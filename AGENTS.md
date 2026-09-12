# Shared code contract

Read README.md and CONSUMERS.md. Preserve current wire layouts and numeric
IDs. Keep the schema, options and maintained nanopb bindings consistent.
Add only code whose output is stored in files or must be identical in more
than one consumer. Do not add sampler engine, platform or UI dependencies.
Run standalone tests and the affected consumer compatibility checks.
All first-party additions must remain MIT compatible.
