# SpecUtils Standalone Example

A minimal example showing how to use the SpecUtils WASM library with separate `<script src>` includes, suitable for serving over HTTP and customizing for your own application.

## Setup

After building (`emcmake cmake -S bindings/wasm -B build_wasm && cmake --build build_wasm`), copy the needed files into one directory:

```bash
mkdir my_app
cp build_wasm/specutils_wasm.js my_app/
cp bindings/wasm/specutils_web.js my_app/
cp bindings/wasm/examples/standalone/specutils_example.html my_app/
```

## Running

Serve the directory over HTTP (required — `file://` URLs can't load `.js` modules):

```bash
cd my_app
python3 -m http.server 8000
```

Then open http://localhost:8000/specutils_example.html in your browser.

## Files

| File | Purpose |
|------|---------|
| `specutils_wasm.js` | Emscripten module with embedded WASM (exports `SpecUtilsModule`) |
| `specutils_web.js` | JS API wrapper (`SpecUtilsAPI` class, helpers) |
| `specutils_example.html` | Working example — upload, view info, export |

## Usage in Your Own Code

```html
<script src="specutils_wasm.js"></script>
<script src="specutils_web.js"></script>
<script>
  SpecUtilsModule().then(function(Module) {
    var api = new SpecUtilsAPI(Module);

    // Load a file (e.g., from an <input type="file">)
    var info = api.loadFile(uint8Array, 'spectrum.n42');
    console.log(info.samples, info.gammaDetectors, info.metadata);

    // Get chart data for D3 visualization
    var chartData = api.getChartData(info.samples, info.gammaDetectors);

    // Export to a different format
    var csvBytes = api.exportFile(1);  // 1 = CSV
    downloadFile(csvBytes, 'spectrum.csv');
  });
</script>
```
