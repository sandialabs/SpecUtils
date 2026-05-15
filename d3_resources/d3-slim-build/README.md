# d3-slim-build

A reproducible build of a **feature-minimized d3 v3.5.17** bundle. The build
output overwrites `../d3.v3.min.js` (the file loaded by every chart in
InterSpec and embedded into every self-contained HTML report it generates).
Stock d3 v3.5.17 is 153 KB minified; this slim build is ~66 KB.

The directory ships only the inputs: a build script, a custom `smash` entry
point, six small patch files, a verification script, and a license header.
The d3 v3 source itself is **not vendored** — `build_d3_slim.sh` downloads
the official tarball from github.com/d3/d3 each time it runs.

## Quick start

```sh
cd external_libs/SpecUtils/d3_resources/d3-slim-build
bash build_d3_slim.sh --install
```

That fetches the upstream tarball, builds the slim bundle, verifies the
public API surface, and overwrites `../d3.v3.min.js`. Rebuild InterSpec
normally; CMake will redeploy the new bundle into `InterSpec_resources/`.

## Requirements

| Tool | Why | Tested with |
|------|-----|-------------|
| `bash` | the build script | macOS / Linux |
| `curl` | download upstream tarball | any modern version |
| `tar` | extract tarball | any modern version |
| `shasum` | verify tarball integrity (`shasum -a 256`) | macOS / coreutils |
| `node` | run `bin/start`, `smash`, and `verify_api.js` | v18+ recommended (tested on v25.9) |
| `npm` | one-shot install of `smash` and `uglify-js` into a temp dir | tested on 11.x |

No global installs are needed. `smash@0.0.15` and `uglify-js@2.6.2` are
pinned in `package.json` and installed into a `mktemp -d` work directory
that's removed at the end of the build.

## Command-line flags

```
bash build_d3_slim.sh [--install] [--keep-work] [-h|--help]
```

- (no flags) — produce `output/d3.v3.min.js` inside this directory. Does
  **not** touch the live `../d3.v3.min.js`.
- `--install` — after a successful build, overwrite `../d3.v3.min.js` with
  the slim bundle.
- `--keep-work` — preserve the `mktemp -d` work directory (downloaded
  tarball + extracted source + `node_modules`) for inspection. Useful when
  diagnosing build failures.

The build is idempotent: running it twice with the same pinned d3 version,
node, and uglify-js produces byte-identical output.

## What this directory contains

```
d3-slim-build/
  README.md              <- this file
  build_d3_slim.sh       <- builder
  d3-slim.js             <- custom smash entry point
  package.json           <- pins smash + uglify-js for npm install
  license-header.js      <- prepended to the minified output
  verify_api.js          <- post-build API sanity check
  .gitignore             <- excludes work/, output/, node_modules/
  patches/
    behavior-index.js    <- replaces src/behavior/index.js
    scale-index.js       <- replaces src/scale/index.js
    svg-index.js         <- replaces src/svg/index.js
    math-index.js        <- replaces src/math/index.js
    locale.js            <- replaces src/locale/locale.js
    en-US.js             <- replaces src/locale/en-US.js
```

The build script applies the six patches by copying them into the extracted
upstream tree (in the temp work directory). Originals on disk in this
directory are never modified.

### Patches

- `behavior-index.js` — drops public `d3.behavior.drag` (unused); keeps
  `zoom`. (`event/drag.js` is a different internal file and remains as a
  zoom dependency.)
- `scale-index.js` — keeps `linear`, `log`, `pow`, `ordinal`, `category`;
  drops `sqrt`, `quantile`, `quantize`, `threshold`, `identity`.
- `svg-index.js` — keeps `arc`, `line`, `area`, `axis`; drops
  `line-radial`, `area-radial`, `chord`, `diagonal`, `diagonal-radial`,
  `symbol`, `brush`.
- `math-index.js` — drops `d3.random.*`; keeps `d3.transform`.
- `locale.js` — drops the `time-format` half of locale (only number-format
  is needed by `d3.format`).
- `en-US.js` — drops date/time keys from the locale definition. Together
  with `locale.js`, severs `d3.format`'s transitive dependency on the
  entire `time/` module.

### Custom entry point

`d3-slim.js` replaces upstream `src/d3.js` and lists the top-level modules
to compile. Smash walks the import graph from this file; modules not
reachable from these imports are excluded.

## What's in / what's out

**Kept** in the slim bundle:

| Module | Reason |
|--------|--------|
| core | foundational utilities; everything depends on it |
| arrays | `d3.min`, `d3.max`, `d3.extent`, `d3.bisector`, plus internal `functor` / `dispatch` / `rebind` / `nest` / `map` / `set` |
| math | `d3.transform`, internal trig helpers |
| color | internal dependency of scale / interpolate |
| interpolate | internal dependency of scale, transition |
| format | `d3.format` (Shielding2DView, D3TimeChart) — patched to skip time-format |
| event | `d3.event`, `d3.mouse`, `d3.touches`, `d3.timer`, internal `event/drag` (zoom dependency) |
| selection | DOM ops — used by every chart |
| transition | `.transition().duration(...)` (ShieldingSourceFitPlot, RelEffPlot, DrfChart) |
| behavior | `d3.behavior.zoom` only |
| scale | linear / log / pow / category10 |
| svg | axis / line / area / arc |
| compat | small browser shims; kept for Safari / older WKWebView |
| geom/point, geom/polygon | pulled transitively by svg/arc, svg/line, svg/area |

**Dropped:**

| Module | Why safe |
|--------|----------|
| layout | no InterSpec JS uses `d3.layout.*` |
| time | no `d3.time.*` calls; `format` patched to skip the time half of locale |
| geo | no `d3.geo.*` calls; `d3.polygonContains` (LeafletRadMap.js) is supplied by the separate `d3-polygon.min.js` |
| geom (rest) | hull / quadtree / voronoi / delaunay / clip-line unused |
| dsv | no CSV/TSV parsing in InterSpec JS |
| xhr | no `d3.xhr` / `d3.json` loaders; Wt handles I/O |
| random | unused |
| behavior/drag | unused (and distinct from internal `event/drag`) |
| svg/{symbol,diagonal,chord,brush,line-radial,area-radial,diagonal-radial} | unused |
| scale/{sqrt,quantile,quantize,threshold,identity} | unused |

## Verification

After concatenation but before installing, `build_d3_slim.sh` runs three
checks:

1. `node --check` on the unminified concatenation (syntax sanity).
2. `verify_api.js` loads the bundle in a stubbed VM context and asserts
   that every API InterSpec calls is present (e.g. `d3.scale.linear`,
   `d3.svg.axis`, `d3.behavior.zoom`, `d3.format`, `d3.bisector`,
   `d3.transform`) and every cut API is `undefined` (`d3.layout`,
   `d3.time`, `d3.geo`, `d3.behavior.drag`, etc.). Also exercises a few
   APIs functionally (scale arithmetic, format directives,
   category10 colors, `d3.svg.{line,area,arc}` path generation).
3. Final byte counts (slim vs live) are printed.

Any failure aborts the build before any output is written.

## Bumping the d3 version

If you want to move from d3 v3.5.17 to another v3.x patch release:

1. Edit `build_d3_slim.sh`, change `D3_VERSION`, and clear
   `D3_TARBALL_SHA256` to an empty string.
2. Run `bash build_d3_slim.sh` once. It prints the SHA-256 of the
   downloaded tarball.
3. Paste that SHA-256 back into `D3_TARBALL_SHA256` to enforce it on
   future runs.
4. If upstream changed the layout of any of the six patched files,
   the build may include unintended modules or fail to apply a patch.
   Run with `--keep-work` and diff the originals (in
   `<tmp>/d3-<version>/src/...`) against the patches in this directory.

Going to d3 v4+ is **not supported** by this build — d3 v4 was a complete
rewrite (different module system, different API surface). The patches and
the `smash` entry point are specific to v3.

## Troubleshooting

- **`Required command 'X' not found in PATH`** — install the listed tool
  (most likely `node`, `npm`, or `shasum`).
- **`FAIL: tarball SHA-256 mismatch`** — either you changed `D3_VERSION`
  without also clearing `D3_TARBALL_SHA256`, or github's tarball encoding
  drifted. Clear the pin, re-run, and re-pin the new value if you trust the
  source.
- **`FAIL: smash not installed` / `uglifyjs not installed`** — npm install
  failed. Check network access; re-run with `--keep-work` and look at the
  npm output in the work dir.
- **`FAIL — N assertion(s) failed` in verify_api** — a patch or the entry
  point excluded a module InterSpec depends on. Re-run with `--keep-work`,
  inspect the unminified `<work>/d3.slim.js`, and adjust the patches or
  `d3-slim.js`. Common cause when bumping versions: upstream renamed a
  module or moved an internal helper.

## Caveats

- The slim bundle is **API-compatible with stock d3 v3** for the surface
  InterSpec uses. It is *not* a drop-in replacement for arbitrary d3 v3
  consumers — anything that calls e.g. `d3.layout.force`, `d3.time.scale`,
  or `d3.svg.brush` will throw `TypeError: ... is not a function`.
- Each rebuild downloads ~3 MB from github.com/d3/d3. The work directory
  reaches ~50 MB during build (extracted source + node_modules) and is
  removed on exit unless you pass `--keep-work`.
- The pinned `uglify-js@2.6.2` is the version d3 v3.5.17 itself shipped
  with. Newer `terser`-derived uglifyjs versions also work but may produce
  slightly different output. CMake's `deploy_js_resource` macro
  (`cmake/DeployJsAndCss.cmake:17-28`) runs `uglifyjs -c` over whatever
  lands at `../d3.v3.min.js`; a double-minify of already-minified input is
  idempotent.
