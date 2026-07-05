# Upstream Data Recommendations

Issues found in `appliance_api_erd_definitions.json` that should be reported to GE
for correction in the source definitions.

## Critical

### Duplicate field names within same ERD

**ERD 0x3230** (Dishwasher): Two entries share the exact `field_name`
`DishKey_SaniAndSteam` but map to different bit positions (offset 54 vs. offset 35).
This causes ambiguity in downstream processing — entity names collide and only one
can be generated.

**Recommendation:** Rename one of the fields to disambiguate (e.g.,
`DishKey_SaniAndSteam_Low` / `DishKey_SaniAndSteam_High` or use the actual
functional name from the engineering spec).

### Type/size mismatches

**3,129 entries** have a `field_type` that does not match the expected size for
that type (e.g., `bool` with `size=2/4/8/16/32`, `u8` with `size=2/4/6/8/10/16/22/32`).

Most are bit-field entries where `field_size` reflects the parent byte allocation
rather than the actual type width. The `field_bits` sub-object provides the actual
bit-level layout.

**Recommendation:** For bit-field entries, set `field_size` to the actual bit width
divided by 8 (rounded up), or document that `field_size` for bit-fields refers to
the parent container size. Currently the pipeline works around this, but it makes
validation and tooling harder.

## Moderate

### Case-sensitive duplicate field names

**297 entries** across 104 groups have `field_name` values that differ only by case
within the same `erd_id` (e.g., `reserved` vs `Reserved` in 0x0802, 0x5080, 0x5081).

**Recommendation:** Standardize field name casing across all ERDs. Recommend
`PascalCase` for field names to match the existing majority convention.

### Non-zero-based enum keys

**24 enum fields** have `field_values` keys that start at 1 instead of 0
(e.g., 0x0004 keys 1,2; 0x1012 keys 1-5,255; 0x2020 keys 1-19,33-162).

**Recommendation:** Document whether 1-based enum keys are intentional for these
ERDs or a data entry error. If intentional, add a note in the ERD description
explaining the offset.

### Scaling factor of zero

**2 entries** (0x301b/Index 0 Time, 0x301b/Index 0 Motor Speed) have
`scaling_factor=0`, which would cause division-by-zero in any consumer that
applies the scaling factor.

**Recommendation:** Set `scaling_factor=1` for fields that don't require scaling,
or remove the field entirely if it's not meant to be exposed.

## Informational

### Raw type fields

**78 entries** with `field_type=raw` (e.g., Configuration Hash, PictureId,
Engineering Snapshot, Padding) have no Home Assistant domain mapping. These are
correctly excluded from discovery.

### Large enum gaps

Some enum fields have large gaps in their value keys (e.g., 0x2020 has keys up to
162 with many gaps). This is valid ERD semantics but generates verbose value
templates.

### Non-ASCII characters

**881 non-ASCII characters** are present in field values and descriptions
(e.g., °F, °C, μg/m³, mN·m, inH₂O). These are valid Unicode in JSON and are
handled correctly by the pipeline.

## Summary

| Severity | Count | Impact |
|----------|-------|--------|
| Critical | 2 | Entity generation failures, data ambiguity |
| Moderate | 3 | Validation noise, maintenance burden |
| Informational | 3 | No action needed |