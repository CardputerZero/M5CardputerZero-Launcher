#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Convert a scoped enum to an explicitly selected type. The expression is
 * evaluated once and the destination type remains visible at the call site.
 */
#ifdef __cplusplus
#define CP0_ENUM_CAST(type, value) (static_cast<type>(value))
#else
#define CP0_ENUM_CAST(type, value) ((type)(value))
#endif

#define CP0_ENUM_CAST_INT(value) CP0_ENUM_CAST(int, value)
#define CP0_ENUM_CAST_SIZE_T(value) CP0_ENUM_CAST(size_t, value)
#define CP0_ENUM_CAST_UINT8(value) CP0_ENUM_CAST(uint8_t, value)
#define CP0_ENUM_CAST_UINT16(value) CP0_ENUM_CAST(uint16_t, value)
#define CP0_ENUM_CAST_UINT32(value) CP0_ENUM_CAST(uint32_t, value)
#define CP0_ENUM_CAST_UINT64(value) CP0_ENUM_CAST(uint64_t, value)
