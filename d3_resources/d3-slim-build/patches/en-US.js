// Replacement for src/locale/en-US.js — drops the date/time keys.
// Paired with the locale.js patch above; together they sever d3.format's
// transitive dependency on the time-format machinery.

import "locale";

var d3_locale_enUS = d3.locale({
  decimal: ".",
  thousands: ",",
  grouping: [3],
  currency: ["$", ""]
});
