#pragma once

/*!
 * @file
 * @brief Lightweight logging helpers used across the bridge implementation.
 *
 * This header is safe to include from any translation unit (C or C++) and
 * intentionally does not define any global symbols — it only provides macros.
 */

#ifdef __cplusplus
#define GEA_MAYBE_UNUSED [[maybe_unused]]
#else
#define GEA_MAYBE_UNUSED __attribute__((unused))
#endif

/// Define a translation-unit-local logging tag that does not trigger
/// -Wunused-const-variable when ESP log macros are compiled out.
#define GEA_TAG(name) GEA_MAYBE_UNUSED static const char* const name
