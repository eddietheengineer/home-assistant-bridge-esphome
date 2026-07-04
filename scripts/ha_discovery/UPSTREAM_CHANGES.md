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

## Remove `write` from EEV Position ERDs

These ERDs are read-only status values reported by the appliance. They should not be writeable. Remove `write` from `erd_operations` for:

| ERD  | Name                    |
|------|-------------------------|
| 0x7512 | EEV1 Desired Position  |
| 0x7513 | EEV2 Desired Position  |
| 0x7514 | EEV1 Actual Position   |
| 0x7515 | EEV2 Actual Position   |

## Remove `write` from Processed Ambient Temperature ERDs

These are read-only processed temperature values. Remove `write` from `erd_operations` for:

| ERD  | Name                                              |
|------|---------------------------------------------------|
| 0x7104 | Processed Inside ambient temperature              |
| 0x7114 | Processed indoor ambient temperature (rounded)    |
| 0x7115 | Processed outdoor ambient temperature (rounded)   |

## Remove `write` from Read-Only Reported Values

These ERDs are read-only status values. Remove `write` from `erd_operations` for:

| ERD  | Name                      |
|------|---------------------------|
| 0x4026 | Actual Setpoint Temperature |
| 0x7907 | Position of Expansion valve EEV0 |
| 0x7938 | Compressor speed target |

## Add Units to Fan Speed ERDs

These ERDs report fan speed in RPM but are missing `unit_of_measurement`. Add `"unit_of_measurement": "rpm"` for:

| ERD  | Name                              |
|------|-----------------------------------|
| 0x7130 | Inside fan speed                 |
| 0x7131 | Outside fan speed                |
| 0x7132 | Inside target fan speed          |
| 0x7133 | Outside target fan speed         |
| 0x7136 | Outdoor Fan 2 Speed              |
| 0x7137 | Outdoor Fan 2 Target Speed       |
| 0x5b13 | Hood Actual Fan Speed            |

## Add Units to Fan PWM ERDs

These ERDs report fan PWM duty cycle as a percentage (0-100). Add `"unit_of_measurement": "%"`, `"scaling_factor": 100` for:

| ERD  | Name              |
|------|-------------------|
| 0x7134 | Inside fan PWM   |
| 0x7135 | Outside fan PWM  |

## Add Units to EEV Position ERDs

These ERDs report stepper motor positions in steps. Add `"unit_of_measurement": "steps"` for:

| ERD  | Name                                      |
|------|-------------------------------------------|
| 0x7512 | EEV1 Desired Position                    |
| 0x7513 | EEV2 Desired Position                    |
| 0x7514 | EEV1 Actual Position                     |
| 0x7515 | EEV2 Actual Position                     |
| 0x7518 | EEV Linear Controller Desired Position   |
| 0x7907 | Position of Expansion valve EEV0         |

## Fix WAC Ambient Temperature Scaling

The ERD description says "Valid Range - min 32(0x20)F max 140(0x8C)F" — the value is already in °F, no scaling needed. The auto-detected scaling factor of 140 is a false positive from matching "140" in the description. Remove `scaling_factor` (set to `null`) for:

| ERD  | Name                    |
|------|-------------------------|
| 0x7a02 | WAC Ambient Temperature |

## Add Scaling to Appliance Cumulative Energy

The ERD reports cumulative energy in Wh but `unit_of_measurement` is kWh. Add `"scaling_factor": 1000` for:

| ERD  | Name                        |
|------|------------------------------|
| 0xd030 | Appliance Cumulative Energy |