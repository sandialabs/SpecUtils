This library was downloaded 20260510 from https://github.com/HowardHinnant/date and corresponds to release v3.0.4 (replaces the prior 20200502 / git e12095f drop).

Local modifications relative to upstream v3.0.4:
  * `CMakeLists.txt`: `cmake_minimum_required` bumped from `VERSION 3.7` to `VERSION 3.15...4.3` to match the rest of the SpecUtils build (CMake 4.x dropped support for `cmake_minimum_required` < 3.5, and the SpecUtils repo standardized on `3.15...4.3`).
  * Upstream `.gitattributes` is intentionally not vendored.

No other modifications have been made.
