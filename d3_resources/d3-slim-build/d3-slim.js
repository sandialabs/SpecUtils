// Custom smash entry point for a feature-minimized d3 v3 bundle.
// Mirrors the structure of upstream src/d3.js but omits modules that
// no InterSpec consumer uses. See README.md for rationale.

import "start";
import "compat/";

import "arrays/";
import "behavior/";
import "color/";
import "core/";
import "event/";
import "format/";
import "interpolate/";
import "math/";
import "scale/";
import "selection/";
import "svg/";
import "transition/";

import "end";
