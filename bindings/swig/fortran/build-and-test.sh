#!/bin/bash

pushd /tmp

rm -rf build

cmake -S /mnt/c/Projects/code/SpecUtils \
 -B build \
 -DSpecUtils_BUILD_UNIT_TESTS=ON \
 -DSpecUtils_BUILD_REGRESSION_TEST=ON \
 -DSpecUtils_ENABLE_EQUALITY_CHECKS=ON \
 -DPERFORM_DEVELOPER_CHECKS=ON \
 -DSpecUtils_ENABLE_D3_CHART=OFF \
 -DSpecUtils_USING_NO_THREADING=ON \
 -DSpecUtils_D3_SUPPORT_FILE_STATIC=OFF \
 -DSpecUtils_ENABLE_URI_SPECTRA=OFF \
 -DCMAKE_BUILD_TYPE=Release \
 -DSpecUtils_FLT_PARSE_METHOD=FastFloat \
 -DSpecUtils_FETCH_FAST_FLOAT=ON \
 -DSpecUtils_C_BINDINGS=ON \
 -DSpecUtils_FORTRAN_SWIG=ON

 cmake --build build -j 20 --config Release

 ctest --rerun-failed --output-on-failure --test-dir build -C Release

 popd