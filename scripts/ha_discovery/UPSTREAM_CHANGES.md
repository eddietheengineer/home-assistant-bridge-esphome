# Recommended Upstream Changes

Changes to propose to the upstream `public-appliance-api-documentation` repository so the source ERD definitions are corrected.

## Remove `write` from Sensor Lock conditional ERDs

These ERDs list `write` in `erd_operations` but are only writable when Sensor Lock ERD (0x7042) is set — a special diagnostic mode. They should not be exposed as writeable in normal operation. Remove `write` from `erd_operations` for:

| ERD  | Name                              |
|------|-----------------------------------|
| 0x7100 | Inside ambient temperature       |
| 0x7101 | Inside coil temperature          |
| 0x7102 | Outside ambient temperature      |
| 0x7103 | Outside coil temperature         |
| 0x7108 | Indoor Coil Vapor Temperature    |
| 0x710a | Outdoor Coil Vapor Temperature   |
| 0x7130 | Inside fan speed                 |
| 0x7131 | Outside fan speed                |
| 0x7132 | Inside target fan speed          |
| 0x7133 | Outside target fan speed         |
| 0x7601 | Inverter Actual Speed RPM        |