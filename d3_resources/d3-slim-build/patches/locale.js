// Replacement for src/locale/locale.js — drops the time-format dependency.
// d3.format only needs the number-format half of the locale, and no InterSpec
// code uses d3.locale().timeFormat or any d3.time.* API. Excluding time-format
// here lets us also drop the entire time/ module from the bundle.

import "number-format";

d3.locale = function(locale) {
  return {
    numberFormat: d3_locale_numberFormat(locale)
  };
};
