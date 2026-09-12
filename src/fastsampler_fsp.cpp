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

bool fsp_pb_write(
    const char* filepath,
    const float* min_peaks,
    const float* max_peaks,
    uint32_t zone_count)
{
	FILE* fp = fopen(filepath, "wb");
	if (!fp)
		return false;

	uint32_t magic = FSP_MAGIC_PB;
	uint32_t size_placeholder = 0;
	fwrite(&magic, sizeof(magic), 1, fp);
	fwrite(&size_placeholder, sizeof(size_placeholder), 1, fp);

	pb_ostream_t stream = fsp_ostream_from_file(fp);

	for (uint32_t i = 0; i < zone_count; ++i) {
		FSPZone zone;
		memset(&zone, 0, sizeof(zone));

		zone.min_peaks_count = ZONE_PEAK_CACHE_POINTS;
		zone.max_peaks_count = ZONE_PEAK_CACHE_POINTS;
		memcpy(zone.min_peaks, min_peaks + i * ZONE_PEAK_CACHE_POINTS, ZONE_PEAK_CACHE_POINTS * sizeof(float));
		memcpy(zone.max_peaks, max_peaks + i * ZONE_PEAK_CACHE_POINTS, ZONE_PEAK_CACHE_POINTS * sizeof(float));

		if (!pb_encode_tag(&stream, PB_WT_STRING, 1))
			goto fail;
		if (!pb_encode_submessage(&stream, FSPZone_fields, &zone))
			goto fail;
	}

	{
		uint32_t payload_size = (uint32_t)stream.bytes_written;
		fseek(fp, sizeof(uint32_t), SEEK_SET);
		fwrite(&payload_size, sizeof(payload_size), 1, fp);
	}

	fclose(fp);
	return true;

fail:
	fclose(fp);
	return false;
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
