// Loads the freshly-built d3 bundle in a stubbed VM context and checks that
// every d3 API InterSpec relies on is present, and every dropped API is absent.
// Usage: node verify_api.js <path-to-d3.js>

const fs = require("fs");
const vm = require("vm");
const path = require("path");

const bundlePath = process.argv[2];
if (!bundlePath) {
  console.error("Usage: node verify_api.js <path-to-d3.js>");
  process.exit(2);
}

const code = fs.readFileSync(bundlePath, "utf8");

// Minimal browser-ish sandbox. d3 v3 reads document.documentElement at load
// time to discover the SVG namespace; we provide just enough to let it boot.
const sandbox = {
  console,
  setTimeout, clearTimeout, setInterval, clearInterval,
  Date, Math, JSON, parseInt, parseFloat, isNaN, isFinite,
  Object, Array, String, Number, Boolean, Function, RegExp, Error,
  TypeError, RangeError, SyntaxError,
  Uint8Array, Uint16Array, Uint32Array, Int8Array, Int16Array, Int32Array,
  Float32Array, Float64Array, ArrayBuffer,
  Map, Set, WeakMap, WeakSet, Promise, Symbol,
  navigator: { userAgent: "node" },
  document: {
    documentElement: {
      namespaceURI: "http://www.w3.org/1999/xhtml",
      style: {}
    },
    createElementNS: function () { return { style: {}, setAttributeNS: function(){}, setAttribute: function(){} }; },
    createElement: function () { return { style: {}, setAttribute: function(){} }; },
    getElementsByTagName: function () { return []; }
  },
  Element: function () {},
  HTMLElement: function () {},
  SVGElement: function () {},
  CSSStyleDeclaration: function () {}
};
sandbox.window = sandbox;
sandbox.self = sandbox;
vm.createContext(sandbox);

try {
  vm.runInContext(code, sandbox, { filename: path.basename(bundlePath) });
} catch (err) {
  console.error("FAIL: bundle threw on load:", err.message);
  process.exit(1);
}

const d3 = sandbox.d3;
if (!d3) {
  console.error("FAIL: bundle did not define `d3` on the sandbox global");
  process.exit(1);
}

const fails = [];

function check(label, cond) {
  if (!cond) fails.push(label);
}

// --- Top-level APIs that MUST exist ---
const kept = [
  "version", "select", "selectAll", "selection",
  "event", "mouse", "touches",
  "min", "max", "extent", "sum", "mean",
  "bisector", "bisect", "bisectLeft", "bisectRight",
  "format",
  "transform",
  "rebind", "functor", "dispatch",
  "ascending", "descending",
  "rgb", "hsl",
  "transition", "ease", "timer",
  "interpolate", "interpolateNumber", "interpolateString",
  "entries", "keys", "values", "map", "set", "merge", "range", "zip"
];
for (const k of kept) check("d3." + k + " (kept)", typeof d3[k] !== "undefined");

// --- Nested kept APIs ---
const keptScale = ["linear", "log", "pow", "ordinal", "category10", "category20", "category20b", "category20c"];
for (const k of keptScale) check("d3.scale." + k + " (kept)", d3.scale && typeof d3.scale[k] === "function");

const keptSvg = ["axis", "line", "area", "arc"];
for (const k of keptSvg) check("d3.svg." + k + " (kept)", d3.svg && typeof d3.svg[k] === "function");

check("d3.behavior.zoom (kept)", d3.behavior && typeof d3.behavior.zoom === "function");
check("d3.behavior.drag (kept)", d3.behavior && typeof d3.behavior.drag === "function");

// --- Top-level APIs that MUST be absent ---
const cut = ["layout", "time", "geo", "csv", "tsv", "xhr", "html", "xml", "text", "random", "dsv"];
for (const k of cut) check("d3." + k + " (cut)", typeof d3[k] === "undefined");

// --- Nested cut APIs ---

const cutSvg = ["symbol", "diagonal", "chord", "brush"];
for (const k of cutSvg) check("d3.svg." + k + " (cut)", !d3.svg || typeof d3.svg[k] === "undefined");

const cutScale = ["sqrt", "quantile", "quantize", "threshold", "identity"];
for (const k of cutScale) check("d3.scale." + k + " (cut)", !d3.scale || typeof d3.scale[k] === "undefined");

// --- Functional smoke tests ---
try {
  const s = d3.scale.linear().domain([0, 100]).range([0, 1]);
  check("scale.linear maps midpoint", Math.abs(s(50) - 0.5) < 1e-9);
  check("scale.linear inverts", Math.abs(s.invert(0.5) - 50) < 1e-9);

  const ls = d3.scale.log().domain([1, 1000]).range([0, 3]);
  check("scale.log maps", Math.abs(ls(10) - 1) < 1e-9);

  const ps = d3.scale.pow().exponent(0.5).domain([0, 100]).range([0, 10]);
  check("scale.pow maps", Math.abs(ps(25) - 5) < 1e-9);

  const c10 = d3.scale.category10();
  check("scale.category10 returns color", typeof c10(0) === "string" && c10(0).charAt(0) === "#");

  check("format '.2e'", d3.format(".2e")(1234.5) === "1.23e+3");
  check("format '.3g'", d3.format(".3g")(1234.5) === "1.23e+3" || d3.format(".3g")(1234.5) === "1.23k");
  check("format '.2f'", d3.format(".2f")(1.234) === "1.23");

  check("max", d3.max([1, 5, 3]) === 5);
  check("min", d3.min([1, 5, 3]) === 1);
  const ext = d3.extent([1, 5, 3]);
  check("extent", ext[0] === 1 && ext[1] === 5);

  const bs = d3.bisector(function (d) { return d.x; }).left;
  check("bisector.left", bs([{x:1},{x:3},{x:5}], 4) === 2);

  check("svg.line() returns function", typeof d3.svg.line() === "function");
  check("svg.area() returns function", typeof d3.svg.area() === "function");
  check("svg.arc() returns function", typeof d3.svg.arc() === "function");
  check("svg.axis() returns function", typeof d3.svg.axis() === "function");

  check("behavior.zoom returns object", typeof d3.behavior.zoom() === "function");
  check("behavior.drag returns object", typeof d3.behavior.drag() === "function");

  // d3.transform parses via an SVGGElement.transform.baseVal which our sandbox
  // can't realistically mock; just verify the function exists. Real validation
  // happens in the browser.
  check("d3.transform is a function", typeof d3.transform === "function");
} catch (err) {
  fails.push("functional smoke test threw: " + err.message);
}

if (fails.length === 0) {
  console.log("All API assertions passed (" + (kept.length + keptScale.length + keptSvg.length + 1 + cut.length + 1 + cutSvg.length + cutScale.length) + " presence/absence checks + functional smoke tests).");
  process.exit(0);
} else {
  console.error("FAIL — " + fails.length + " assertion(s) failed:");
  for (const f of fails) console.error("  - " + f);
  process.exit(1);
}
