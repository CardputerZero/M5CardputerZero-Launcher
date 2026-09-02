# APPLaunch development guidance

## Scoped-enum conversions

`cp0_lvgl` exports the public header `cp0_enum_cast.h`. Include it directly in
any C++ file that uses the conversion macro:

```cpp
#include "cp0_enum_cast.h"
```

Use the general `CP0_ENUM_CAST(target_type, enum_value)` for an explicit
conversion from an `enum class` value. Convenience macros are available for
common targets, including `CP0_ENUM_CAST_INT`, `CP0_ENUM_CAST_SIZE_T`,
`CP0_ENUM_CAST_UINT8`, `CP0_ENUM_CAST_UINT16`, `CP0_ENUM_CAST_UINT32`, and
`CP0_ENUM_CAST_UINT64`:

```cpp
enum class LayoutMetric : int { Width = 320 };

constexpr int width = CP0_ENUM_CAST_INT(LayoutMetric::Width);
constexpr auto count = CP0_ENUM_CAST_SIZE_T(LayoutMetric::Width);
constexpr auto raw = CP0_ENUM_CAST(uint32_t, LayoutMetric::Width);
```

This macro is the shared replacement for repeated enum-only `static_cast`
expressions and conversion helpers that only wrap an enum cast. Use the
type-specific convenience macro when it exists; use the general macro for any
other target type. Do not add a project-local helper solely to wrap one of
these macros; if an existing helper is part of a broader API, keep it and use
the macro in its implementation.

Do not use these macros for pointer casts, arbitrary integer conversions, or
untrusted values read from an external API. Validate and range-check an
external integer before converting it to an enum. Pass a side-effect-free enum
value or enumerator as the macro argument, and do not redefine a project-local
macro with the same name.

The `cp0_lvgl` component publishes `include/` through its `SConstruct`
dependency, so consumers should include `cp0_enum_cast.h` by name rather than
using a relative path into `ext_components`. After migrating a conversion,
keep the surrounding API and numeric behavior unchanged and run the APPLaunch
tests/build.
