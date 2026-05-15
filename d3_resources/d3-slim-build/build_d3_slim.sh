#!/usr/bin/env bash
#
# Build a feature-minimized d3 v3 bundle for InterSpec.
#
# Default: produces output/d3.v3.min.js inside this directory. Does NOT touch
# the live ../d3.v3.min.js. To install the result, pass --install (or copy
# manually).
#
# Requirements: bash, curl, tar, shasum, node, npm.

set -euo pipefail

readonly SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
readonly D3_VERSION="3.5.17"
readonly D3_TARBALL_URL="https://github.com/d3/d3/archive/refs/tags/v${D3_VERSION}.tar.gz"
# SHA-256 of the upstream tarball (verified once after a clean download).
# Empty means "compute and warn; do not enforce".
readonly D3_TARBALL_SHA256="d1d66cc48f6d5a96151980b5080772834a485241ae95e2866f88f359c69b874b"

readonly LIVE_OUTPUT="${SCRIPT_DIR}/../d3.v3.min.js"
readonly STAGING_OUTPUT="${SCRIPT_DIR}/output/d3.v3.min.js"

install_into_live=0
keep_work=0
for arg in "$@"; do
  case "$arg" in
    --install) install_into_live=1 ;;
    --keep-work) keep_work=1 ;;
    -h|--help)
      cat <<'EOF'
Usage: build_d3_slim.sh [--install] [--keep-work]

  --install     After a successful build, overwrite ../d3.v3.min.js with the
                staging output. Without this flag, only output/d3.v3.min.js
                is written.
  --keep-work   Keep the working directory (download + extracted source +
                node_modules) after the build for inspection. Default is to
                delete it on exit.
EOF
      exit 0
      ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      exit 2
      ;;
  esac
done

for cmd in curl tar shasum node npm; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "Required command '${cmd}' not found in PATH." >&2
    exit 1
  fi
done

WORK="$( mktemp -d )"
cleanup() {
  if [ "${keep_work}" -eq 0 ]; then
    rm -rf "${WORK}"
  else
    echo "Kept working dir: ${WORK}"
  fi
}
trap cleanup EXIT

mkdir -p "${SCRIPT_DIR}/output"

echo "[1/9] Downloading d3 v${D3_VERSION} tarball..."
curl -fSL "${D3_TARBALL_URL}" -o "${WORK}/d3.tar.gz"

actual_sha="$( shasum -a 256 "${WORK}/d3.tar.gz" | awk '{print $1}' )"
if [ -n "${D3_TARBALL_SHA256}" ]; then
  if [ "${actual_sha}" != "${D3_TARBALL_SHA256}" ]; then
    echo "FAIL: tarball SHA-256 mismatch." >&2
    echo "  expected: ${D3_TARBALL_SHA256}" >&2
    echo "  got:      ${actual_sha}" >&2
    exit 1
  fi
  echo "       SHA-256 verified: ${actual_sha}"
else
  echo "       SHA-256 (record this in build_d3_slim.sh on first run): ${actual_sha}"
fi

echo "[2/9] Extracting..."
tar -xzf "${WORK}/d3.tar.gz" -C "${WORK}"
SRC="${WORK}/d3-${D3_VERSION}"

if [ ! -d "${SRC}/src" ]; then
  echo "FAIL: extracted tree has no src/ directory at ${SRC}" >&2
  exit 1
fi

echo "[3/9] Applying patches to extracted source..."
cp "${SCRIPT_DIR}/d3-slim.js"                "${SRC}/src/d3-slim.js"
cp "${SCRIPT_DIR}/patches/behavior-index.js" "${SRC}/src/behavior/index.js"
cp "${SCRIPT_DIR}/patches/scale-index.js"    "${SRC}/src/scale/index.js"
cp "${SCRIPT_DIR}/patches/svg-index.js"      "${SRC}/src/svg/index.js"
cp "${SCRIPT_DIR}/patches/locale.js"         "${SRC}/src/locale/locale.js"
cp "${SCRIPT_DIR}/patches/en-US.js"          "${SRC}/src/locale/en-US.js"
cp "${SCRIPT_DIR}/patches/math-index.js"     "${SRC}/src/math/index.js"

echo "[4/9] Installing smash + uglify-js into work dir..."
cp "${SCRIPT_DIR}/package.json" "${WORK}/package.json"
( cd "${WORK}" && npm install --no-audit --no-fund --silent --no-package-lock )

readonly SMASH="${WORK}/node_modules/.bin/smash"
readonly UGLIFYJS="${WORK}/node_modules/.bin/uglifyjs"
[ -x "${SMASH}" ]    || { echo "FAIL: smash not installed" >&2; exit 1; }
[ -x "${UGLIFYJS}" ] || { echo "FAIL: uglifyjs not installed" >&2; exit 1; }

echo "[5/9] Generating src/start.js..."
node "${SRC}/bin/start" > "${SRC}/src/start.js"

echo "[6/9] Running smash on src/d3-slim.js..."
"${SMASH}" "${SRC}/src/d3-slim.js" > "${WORK}/d3.slim.js"

echo "[7/9] Syntax check..."
node --check "${WORK}/d3.slim.js"

echo "[8/9] Minifying..."
"${UGLIFYJS}" "${WORK}/d3.slim.js" -c -m -o "${WORK}/d3.slim.min.js"

cat "${SCRIPT_DIR}/license-header.js" "${WORK}/d3.slim.min.js" > "${STAGING_OUTPUT}"

echo "[9/9] Verifying public API surface..."
node "${SCRIPT_DIR}/verify_api.js" "${WORK}/d3.slim.js"

unminified_bytes=$( wc -c < "${WORK}/d3.slim.js" )
minified_bytes=$( wc -c < "${STAGING_OUTPUT}" )
live_bytes=0
if [ -f "${LIVE_OUTPUT}" ]; then
  live_bytes=$( wc -c < "${LIVE_OUTPUT}" )
fi

echo ""
echo "Build complete."
echo "  Staging output: ${STAGING_OUTPUT}"
printf "  Slim unminified:   %8d bytes\n" "${unminified_bytes}"
printf "  Slim minified:     %8d bytes (with license header)\n" "${minified_bytes}"
if [ "${live_bytes}" -gt 0 ]; then
  printf "  Live (existing):   %8d bytes\n" "${live_bytes}"
  pct=$(( (live_bytes - minified_bytes) * 100 / live_bytes ))
  printf "  Reduction:         %d%%\n" "${pct}"
fi

if [ "${install_into_live}" -eq 1 ]; then
  echo ""
  echo "Installing into ${LIVE_OUTPUT}"
  cp "${STAGING_OUTPUT}" "${LIVE_OUTPUT}"
  echo "Done."
else
  echo ""
  echo "Live d3.v3.min.js NOT touched."
  echo "To install: cp ${STAGING_OUTPUT} ${LIVE_OUTPUT}"
  echo "        or: bash ${BASH_SOURCE[0]} --install"
fi
