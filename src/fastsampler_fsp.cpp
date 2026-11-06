// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pb_encode.h>
#include <pb_decode.h>
#include "../include/fastsampler_pb.h"
#include "../include/fastsampler.pb.h"

static bool fsp_file_write_stream_callback(pb_ostream_t* stream, const uint8_t* buf, size_t count)
{
	FILE* fp = (FILE*)stream->state;
	return fwrite(buf, 1, count, fp) == count;
}

static pb_ostream_t fsp_ostream_from_file(FILE* fp)
{
	pb_ostream_t stream = {0};
	stream.callback = fsp_file_write_stream_callback;
	stream.state = fp;
	stream.max_size = SIZE_MAX;
	stream.bytes_written = 0;
	return stream;
}

bool fsp_pb_write_zones(
    const char* filepath,
    uint32_t zone_count,
    FSPZoneSource source,
    void* user)
{
	if (!source)
		return false;
	FILE* fp = fopen(filepath, "wb");
	if (!fp)
		return false;

	uint32_t magic = FSP_MAGIC_PB;
	uint32_t size_placeholder = 0;
	bool ok = fwrite(&magic, sizeof(magic), 1, fp) == 1 && fwrite(&size_placeholder, sizeof(size_placeholder), 1, fp) == 1;

	pb_ostream_t stream = fsp_ostream_from_file(fp);

	// The zone is too large for some thread stacks.
	FSPZone* zone = ok ? (FSPZone*)malloc(sizeof(FSPZone)) : nullptr;
	ok = ok && zone;
	for (uint32_t i = 0; ok && i < zone_count; ++i) {
		memset(zone, 0, sizeof(*zone));
		zone->min_peaks_count = ZONE_PEAK_CACHE_POINTS;
		zone->max_peaks_count = ZONE_PEAK_CACHE_POINTS;
		ok = source(user, i, zone->min_peaks, zone->max_peaks) &&
		     pb_encode_tag(&stream, PB_WT_STRING, 1) &&
		     pb_encode_submessage(&stream, FSPZone_fields, zone);
	}
	free(zone);

	if (ok) {
		uint32_t payload_size = (uint32_t)stream.bytes_written;
		ok = fseek(fp, sizeof(uint32_t), SEEK_SET) == 0 && fwrite(&payload_size, sizeof(payload_size), 1, fp) == 1;
	}
	if (fclose(fp) != 0)
		ok = false;
	return ok;
}

struct FSPArraySource {
	const float* min_peaks;
	const float* max_peaks;
};

static bool fsp_array_source(void* user, uint32_t zone, float* out_min_peaks, float* out_max_peaks)
{
	const FSPArraySource* arrays = (const FSPArraySource*)user;
	memcpy(out_min_peaks, arrays->min_peaks + (size_t)zone * ZONE_PEAK_CACHE_POINTS, ZONE_PEAK_CACHE_POINTS * sizeof(float));
	memcpy(out_max_peaks, arrays->max_peaks + (size_t)zone * ZONE_PEAK_CACHE_POINTS, ZONE_PEAK_CACHE_POINTS * sizeof(float));
	return true;
}

bool fsp_pb_write(
    const char* filepath,
    const float* min_peaks,
    const float* max_peaks,
    uint32_t zone_count)
{
	FSPArraySource arrays = { min_peaks, max_peaks };
	return fsp_pb_write_zones(filepath, zone_count, fsp_array_source, &arrays);
}

struct FSPDecodeContext {
	float* min_peaks;
	float* max_peaks;
	uint32_t max_zones;
	uint32_t zone_idx;
};

static bool fsp_zone_decode_callback(pb_istream_t* stream, const pb_field_t* field, void** arg)
{
	(void)field;
	FSPDecodeContext* ctx = (FSPDecodeContext*)*arg;

	FSPZone zone;
	memset(&zone, 0, sizeof(zone));
	if (!pb_decode(stream, FSPZone_fields, &zone))
		return false;

	if (ctx->zone_idx >= ctx->max_zones)
		return true;

	memcpy(ctx->min_peaks + ctx->zone_idx * ZONE_PEAK_CACHE_POINTS, zone.min_peaks, sizeof(float) * ZONE_PEAK_CACHE_POINTS);
	memcpy(ctx->max_peaks + ctx->zone_idx * ZONE_PEAK_CACHE_POINTS, zone.max_peaks, sizeof(float) * ZONE_PEAK_CACHE_POINTS);
	ctx->zone_idx++;
	return true;
}

bool fsp_pb_read(
    const char* filepath,
    float* out_min_peaks,
    float* out_max_peaks,
    uint32_t max_zones,
    uint32_t* out_zone_count)
{
	FILE* fp = fopen(filepath, "rb");
	if (!fp)
		return false;

	uint32_t magic, payload_size;
	if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != FSP_MAGIC_PB) {
		fclose(fp);
		return false;
	}
	if (fread(&payload_size, sizeof(payload_size), 1, fp) != 1 || payload_size == 0) {
		fclose(fp);
		return false;
	}

	uint8_t* buf = (uint8_t*)malloc(payload_size);
	if (!buf) {
		fclose(fp);
		return false;
	}
	if (fread(buf, 1, payload_size, fp) != payload_size) {
		fclose(fp);
		free(buf);
		return false;
	}
	fclose(fp);

	FSPDecodeContext ctx;
	ctx.min_peaks = out_min_peaks;
	ctx.max_peaks = out_max_peaks;
	ctx.max_zones = max_zones;
	ctx.zone_idx  = 0;

	FSPFile fsp_msg;
	memset(&fsp_msg, 0, sizeof(fsp_msg));
	fsp_msg.zones.funcs.decode = fsp_zone_decode_callback;
	fsp_msg.zones.arg = &ctx;

	pb_istream_t stream = pb_istream_from_buffer(buf, payload_size);
	bool ok = pb_decode(&stream, FSPFile_fields, &fsp_msg);
	free(buf);

	if (!ok)
		return false;

	*out_zone_count = ctx.zone_idx;
	return true;
}

// =============================================================================
// A varint of at most 10 bytes from buf, which holds avail bytes. Return the
// number of bytes it takes, or 0 when it does not end in them.
// =============================================================================
static size_t fsp_varint(const uint8_t* buf, size_t avail, uint64_t* out)
{
	uint64_t value = 0;
	for (size_t i = 0; i < avail && i < 10; ++i) {
		value |= (uint64_t)(buf[i] & 0x7f) << (7 * i);
		if ((buf[i] & 0x80) == 0) {
			*out = value;
			return i + 1;
		}
	}
	return 0;
}

void fsp_pb_index_free(FSPIndex* index)
{
	if (!index)
		return;
	free(index->offsets);
	free(index->lengths);
	memset(index, 0, sizeof(*index));
}

static bool fsp_index_push(FSPIndex* index, uint32_t* capacity, uint32_t offset, uint32_t length)
{
	// =============================================================================
	// While the records are the same length at a fixed distance, the index
	// keeps only the first, the length and the distance.
	// =============================================================================
	if (!index->offsets) {
		bool uniform = index->zone_count == 0 || length == index->length;
		if (uniform && index->zone_count == 1)
			index->stride = offset - index->first;
		else if (uniform && index->zone_count > 1)
			uniform = offset == index->first + index->zone_count * index->stride;
		if (uniform) {
			if (index->zone_count == 0) {
				index->first = offset;
				index->length = length;
			}
			index->zone_count++;
			return true;
		}
		*capacity = index->zone_count + 64;
		index->offsets = (uint32_t*)malloc(*capacity * sizeof(uint32_t));
		index->lengths = (uint32_t*)malloc(*capacity * sizeof(uint32_t));
		if (!index->offsets || !index->lengths)
			return false;
		for (uint32_t z = 0; z < index->zone_count; ++z) {
			index->offsets[z] = index->first + z * index->stride;
			index->lengths[z] = index->length;
		}
		index->stride = 0;
	}
	if (index->zone_count == *capacity) {
		uint32_t grown = *capacity * 2;
		uint32_t* offsets = (uint32_t*)realloc(index->offsets, grown * sizeof(uint32_t));
		if (offsets)
			index->offsets = offsets;
		uint32_t* lengths = (uint32_t*)realloc(index->lengths, grown * sizeof(uint32_t));
		if (lengths)
			index->lengths = lengths;
		if (!offsets || !lengths)
			return false;
		*capacity = grown;
	}
	index->offsets[index->zone_count] = offset;
	index->lengths[index->zone_count] = length;
	index->zone_count++;
	return true;
}

bool fsp_pb_index(const char* filepath, FSPIndex* out_index)
{
	if (!out_index)
		return false;
	memset(out_index, 0, sizeof(*out_index));
	FILE* fp = fopen(filepath, "rb");
	if (!fp)
		return false;

	uint32_t header[2];
	bool ok = fread(header, sizeof(header), 1, fp) == 1 && header[0] == FSP_MAGIC_PB && header[1] != 0 &&
	          fseek(fp, 0, SEEK_END) == 0;
	long file_size = ok ? ftell(fp) : -1;
	ok = ok && file_size >= 8 && (uint64_t)header[1] <= (uint64_t)file_size - 8;

	// =============================================================================
	// Walk the fields of the payload by their headers. A field other than a
	// zone is skipped, as the decoder skips it.
	// =============================================================================
	const uint64_t end = 8 + (uint64_t)header[1];
	uint64_t pos = 8;
	uint32_t capacity = 0;
	while (ok && pos < end) {
		uint8_t buf[20];
		size_t avail = (size_t)(end - pos < sizeof(buf) ? end - pos : sizeof(buf));
		ok = fseek(fp, (long)pos, SEEK_SET) == 0 && fread(buf, 1, avail, fp) == avail;
		uint64_t tag = 0;
		size_t tag_bytes = ok ? fsp_varint(buf, avail, &tag) : 0;
		ok = tag_bytes != 0 && (tag >> 3) != 0;
		if (!ok)
			break;
		uint32_t wire = (uint32_t)(tag & 7);
		uint64_t field = tag >> 3;
		uint64_t value = 0;
		size_t value_bytes = 0;
		uint64_t skip = 0;
		switch (wire) {
		case PB_WT_VARINT:
			value_bytes = fsp_varint(buf + tag_bytes, avail - tag_bytes, &value);
			ok = value_bytes != 0;
			break;
		case PB_WT_64BIT:
			skip = 8;
			break;
		case PB_WT_32BIT:
			skip = 4;
			break;
		case PB_WT_STRING:
			value_bytes = fsp_varint(buf + tag_bytes, avail - tag_bytes, &value);
			ok = value_bytes != 0;
			skip = value;
			break;
		default:
			ok = false;
			break;
		}
		uint64_t body = pos + tag_bytes + value_bytes;
		ok = ok && body <= end && skip <= end - body;
		if (ok && field == 1) {
			ok = wire == PB_WT_STRING && fsp_index_push(out_index, &capacity, (uint32_t)body, (uint32_t)skip);
		}
		pos = body + skip;
	}
	fclose(fp);
	if (!ok)
		fsp_pb_index_free(out_index);
	return ok;
}

bool fsp_pb_read_zone(
    const char* filepath,
    const FSPIndex* index,
    uint32_t zone,
    float* out_min_peaks,
    float* out_max_peaks)
{
	if (!index || zone >= index->zone_count || !out_min_peaks || !out_max_peaks)
		return false;
	uint32_t offset = index->offsets ? index->offsets[zone] : index->first + zone * index->stride;
	uint32_t length = index->lengths ? index->lengths[zone] : index->length;

	FILE* fp = fopen(filepath, "rb");
	if (!fp)
		return false;
	uint8_t* buf = (uint8_t*)malloc(length ? length : 1);
	bool ok = buf && fseek(fp, (long)offset, SEEK_SET) == 0 && fread(buf, 1, length, fp) == length;
	fclose(fp);

	FSPZone* decoded = ok ? (FSPZone*)calloc(1, sizeof(FSPZone)) : nullptr;
	if (decoded) {
		pb_istream_t stream = pb_istream_from_buffer(buf, length);
		ok = pb_decode(&stream, FSPZone_fields, decoded);
		if (ok) {
			memcpy(out_min_peaks, decoded->min_peaks, sizeof(float) * ZONE_PEAK_CACHE_POINTS);
			memcpy(out_max_peaks, decoded->max_peaks, sizeof(float) * ZONE_PEAK_CACHE_POINTS);
		}
	} else {
		ok = false;
	}
	free(decoded);
	free(buf);
	return ok;
}
