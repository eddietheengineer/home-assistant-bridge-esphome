#pragma once

// Logging macros only — no global symbols, safe to include from any TU.

#ifdef __cplusplus
#define GEA_MAYBE_UNUSED [[maybe_unused]]
#else
#define GEA_MAYBE_UNUSED __attribute__((unused))
#endif

/// Define a translation-unit-local logging tag that does not trigger
/// -Wunused-const-variable when ESP log macros are compiled out.
#define GEA_TAG(name) GEA_MAYBE_UNUSED static const char* const name
