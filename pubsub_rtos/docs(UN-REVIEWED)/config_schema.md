# Runtime Config Schema

The runtime config is stored as a versioned `DeviceConfig_t` record with CRC32 validation.

Fields:

- `magic`: identifies the schema family.
- `version`: current schema version.
- `size`: expected structure size.
- `tempThreshold`: temperature alert threshold.
- `lowVoltageDecivolts`: low-voltage threshold.
- `diagnosticPeriodMs`: diagnostics publishing period.
- `framePeriodMs`: frame-ready publishing period.
- `bootCount`: persistent boot counter.
- `crc32`: CRC over the record except the CRC field.

The loader supports default fallback and migration from the earlier v1 record.
