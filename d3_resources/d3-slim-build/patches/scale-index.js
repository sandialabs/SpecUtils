// Replacement for src/scale/index.js — keeps only the scale types InterSpec uses.
// Dropped: sqrt, quantile, quantize, threshold, identity.
// (ordinal is kept because category10 depends on it.)

import "scale";
import "linear";
import "log";
import "pow";
import "ordinal";
import "category";
