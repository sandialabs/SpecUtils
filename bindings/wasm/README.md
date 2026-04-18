# SpecUtils WebAssembly Build

This directory builds the SpecUtils library to WebAssembly using Emscripten, producing:
- Self-contained HTML files for browser-based spectrum file viewing and conversion
- A Node.js command-line tool for sandboxed spectrum file conversion

## Output Files

- **SpecUtilsWebViewer.html**: Full-featured viewer with interactive D3 spectrum chart, sample/detector selection, and file export
- **SpecUtilsMinimalExample.html**: Simple example showing file metadata — use as a starting point for custom integrations
- **specutils_cli.js**: Command-line tool for converting spectrum files to N42-2012, runs via Node.js

The HTML files are completely self-contained (JS, CSS, and WASM are all inlined).

## Building

### Prerequisites
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (3.1.51+)
- CMake 3.15+
- Python 3
- Node.js (for the CLI tool)

### Build Steps
```bash
# Activate Emscripten
source /path/to/emsdk/emsdk_env.sh

# Configure and build
emcmake cmake -S bindings/wasm -B build_wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm

# Open viewer in browser
open build_wasm/SpecUtilsWebViewer.html

# Use CLI tool
node build_wasm/specutils_cli.js input.spc output.n42
```

## Command-Line Tool

The CLI tool converts any supported spectrum file format to N42-2012. It runs via Node.js, providing sandboxed execution of the WASM-compiled SpecUtils library.

```bash
# Basic usage
node specutils_cli.js <input_file> [output_file.n42]

# If output is omitted, replaces the input extension with .n42
node specutils_cli.js spectrum.spc           # writes spectrum.n42
node specutils_cli.js data.pcf output.n42    # writes output.n42
```

The tool prints a summary of the parsed file (measurements, detectors, manufacturer, timing, counts) before writing the output.

## JavaScript API

The `specutils_web.js` module provides a clean JavaScript API on top of the WASM C bindings:

```javascript
// Initialize WASM module
const Module = await SpecUtilsModule();
const api = new SpecUtilsAPI(Module);

// Load a spectrum file
const fileBytes = new Uint8Array(arrayBuffer);
const info = api.loadFile(fileBytes, 'spectrum.n42');
// info = { samples: [1,2,...], detectors: ['Det1',...], metadata: {...}, ... }

// Get D3 chart data for selected samples/detectors
const chartData = api.getChartData([1, 2], ['Det1', 'Det2']);
// chartData = { spectra: [{ x: [...], y: [...], liveTime, realTime, ... }] }

// Export to a different format
const exportedBytes = api.exportFile(4);  // 4 = N42-2012
downloadFile(exportedBytes, 'output.n42');

// Cleanup
api.destroy();
```

### Data I/O

Two patterns for getting data in/out of WASM:

**Pattern 1: Buffer API (recommended)**
```javascript
// Loading: pass ArrayBuffer directly
const bytes = new Uint8Array(arrayBuffer);
api.loadFile(bytes, 'file.spc');

// Exporting: get bytes back
const output = api.exportFile(formatIndex);  // returns Uint8Array
```

**Pattern 2: MEMFS (full filesystem control)**
```javascript
// Write to Emscripten virtual filesystem
Module.FS.writeFile('/tmp/input.spc', fileBytes);
// Use C bindings directly via cwrap
const loadFn = Module.cwrap('SpecFile_load_file', 'boolean', ['number', 'string']);
loadFn(specFilePtr, '/tmp/input.spc');
```

## Export Formats

| Index | Format | Extension |
|-------|--------|-----------|
| 0 | TXT | .txt |
| 1 | CSV | .csv |
| 2 | PCF | .pcf |
| 3 | N42-2006 | .n42 |
| 4 | N42-2012 | .n42 |
| 5 | CHN (ORTEC) | .chn |
| 6 | SPC (Binary Int) | .spc |
| 7 | SPC (Binary Float) | .spc |
| 8 | SPC (ASCII) | .spc |
| 9 | GR-130 | .gr130 |
| 10 | GR-135 | .gr135 |
| 11 | SPE (IAEA) | .spe |
| 12 | CNF (Canberra) | .cnf |
| 13 | TKA | .tka |
