This library was downloaded 20260510 from https://github.com/HowardHinnant/date and corresponds to release v3.0.4 (replaces the prior 20200502 / git e12095f drop).

Local modifications relative to upstream v3.0.4:
  * `CMakeLists.txt`: `cmake_minimum_required` bumped from `VERSION 3.7` to `VERSION 3.15...4.3` to match the rest of the SpecUtils build (CMake 4.x dropped support for `cmake_minimum_required` < 3.5, and the SpecUtils repo standardized on `3.15...4.3`).
  * `include/date/date.h`: in the `#else // !defined _MSC_VER` half of the `parse()` overload set
    (lines ~8227-8332), the nine references to `from_stream` were qualified to `date::from_stream`.
    Needed to build at C++20 or newer with libstdc++: C++20 added `std::chrono::from_stream`, and
    since the `Parsable` argument is a `std::chrono::time_point`, ADL finds both it and
    `date::from_stream`, making the call ambiguous.  Apple's libc++ does not implement `std::chrono::from_stream`,
    which is why this only bites on Linux/GCC and not on macOS.  Verified with GCC 14: unpatched,
    `-std=c++14` and `-std=c++17` compile while `-std=c++20` and `-std=c++23` fail; patched, all
    four compile (as do C++17 and C++20 with Apple clang).
    Upstream already applies exactly this qualification in the `#ifdef _MSC_VER` half of the same
    overload set but never mirrored it into the non-MSVC half, as of v3.0.5.
  * Upstream `.gitattributes` is intentionally not vendored.

No other modifications have been made.
