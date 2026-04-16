#!/usr/bin/env python3
"""Assemble self-contained SpecUtilsWebViewer.html from WASM JS, D3, chart JS/CSS, and HTML template.

Usage:
  python3 assemble_html.py \
    --wasm-js build/specutils_wasm.js \
    --d3-js ../../d3_resources/d3.v3.min.js \
    --chart-js ../../d3_resources/SpectrumChartD3.js \
    --chart-css ../../d3_resources/SpectrumChartD3.css \
    --app-js specutils_web.js \
    --viewer-template specutils_web.html \
    --output-dir build/
"""

import argparse
import os


def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()


def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"  Wrote {path} ({len(content):,} bytes)")


def inline_replace(html, marker, content, tag_type='script'):
    """Replace a <!-- MARKER --> comment with inlined content."""
    if tag_type == 'script':
        replacement = f'<script>\n{content}\n</script>'
    elif tag_type == 'style':
        replacement = f'<style>\n{content}\n</style>'
    else:
        replacement = content

    result = html.replace(f'<!-- {marker} -->', replacement)
    if result == html:
        print(f"  Warning: marker '<!-- {marker} -->' not found in template")
    return result


def main():
    parser = argparse.ArgumentParser(description='Assemble self-contained SpecUtils HTML viewer')
    parser.add_argument('--wasm-js', required=True,
                        help='Emscripten-generated .js with embedded WASM (SINGLE_FILE mode)')
    parser.add_argument('--d3-js', required=True,
                        help='Path to d3.v3.min.js')
    parser.add_argument('--chart-js', required=True,
                        help='Path to SpectrumChartD3.js')
    parser.add_argument('--chart-css', required=True,
                        help='Path to SpectrumChartD3.css')
    parser.add_argument('--app-js', required=True,
                        help='Path to specutils_web.js (JS glue layer)')
    parser.add_argument('--viewer-template', required=True,
                        help='Path to specutils_web.html template')
    parser.add_argument('--output-dir', required=True,
                        help='Output directory for assembled HTML file')
    args = parser.parse_args()

    print("Reading input files...")
    wasm_js = read_file(args.wasm_js)
    d3_js = read_file(args.d3_js)
    chart_js = read_file(args.chart_js)
    chart_css = read_file(args.chart_css)
    app_js = read_file(args.app_js)
    viewer_template = read_file(args.viewer_template)

    os.makedirs(args.output_dir, exist_ok=True)

    print("\nAssembling SpecUtilsWebViewer.html...")
    viewer = viewer_template
    viewer = inline_replace(viewer, 'CHART_CSS', chart_css, 'style')
    viewer = inline_replace(viewer, 'D3_JS', d3_js, 'script')
    viewer = inline_replace(viewer, 'CHART_JS', chart_js, 'script')
    viewer = inline_replace(viewer, 'WASM_JS', wasm_js, 'script')
    viewer = inline_replace(viewer, 'APP_JS', app_js, 'script')

    viewer_path = os.path.join(args.output_dir, 'SpecUtilsWebViewer.html')
    write_file(viewer_path, viewer)

    print("\nDone!")


if __name__ == '__main__':
    main()
