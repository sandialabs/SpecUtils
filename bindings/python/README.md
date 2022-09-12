```bash
cd SpecUtils
mkdir build_python
cd build_python
cmake -DSpecUtils_PYTHON_BINDINGS=ON -DCMAKE_PREFIX_PATH=/path/to/boost ..
cmake --build . --config Release -j8

# macOS
mkdir SpecUtils
cp ../bindings/python/__init__.py ./SpecUtils/
cp libSpecUtils.dylib ./SpecUtils/SpecUtils.so
ln -s ../bindings/python/setup.py .
python3 setup.py bdist_wheel
python3 -m pip install --user --force dist/SpecUtils-0.0.1-cp39-cp39-macosx_12_0_x86_64.whl

# Windows
mkdir SpecUtils
cp .\Release\libSpecUtils.dll .\SpecUtils\SpecUtils.pyd
cp ..\bindings\python\__init__.py .\SpecUtils\
cp ..\bindings\python\setup.py .
python.exe setup.py bdist_wheel
python.exe -m pip install --user --force .\dist\SpecUtils-0.0.1-cp310-cp310-win_amd64.whl
```