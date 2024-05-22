Instructions to build and install a python wheel file.

It could be more automated, or polished, or whatever, but this works.


## macOS build instructions 
Tested on M1 on Ventura, with Xcode 15.1
```bash
# Setup deployment target and path where you will install boost
export MACOSX_DEPLOYMENT_TARGET=10.13
export MY_BOOST_PREFIX=/path/to/install/prefix/to/macOS_boost_1_85_prefix


# Download and build boost with Python support (any non-ancient version of boost should work)
cd /tmp/
curl -L https://boostorg.jfrog.io/artifactory/main/release/1.85.0/source/boost_1_85_0.tar.gz -o boost_1_85_0.tar.gz
tar -xzvf boost_1_85_0.tar.gz
cd boost_1_85_0

# build the b2 executable
./bootstrap.sh cxxflags="--arch arm64 -mmacosx-version-min=10.13" cflags="-arch arm64 -mmacosx-version-min=10.13" linkflags="-arch arm64 -mmacosx-version-min=10.13" --prefix=${MY_BOOST_PREFIX}

# point boost to the Python install we want to use, by using a user-config.jam file
#  Here I'm using the brew install of python 3.11
echo "using python : 3.11.6 : /opt/homebrew/opt/python3/bin/python3 : /opt/homebrew/opt/python3/Frameworks/Python.framework/Versions/3.11/include/python3.11/ : /opt/homebrew/opt/python3/Frameworks/Python.framework/Versions/3.11/lib/libpython3.11.dylib ;" > user-config.jam

# build and install boost for arm64 - we will only make static libs, so this way the 
#  final SpecUtils module library will be self contained.
./b2 --user-config=./user-config.jam toolset=clang-darwin target-os=darwin architecture=arm abi=aapcs cxxflags="-stdlib=libc++ -arch arm64 -std=c++14 -mmacosx-version-min=10.13" cflags="-arch arm64  -mmacosx-version-min=10.13" linkflags="-stdlib=libc++ -arch arm64 -std=c++14 -mmacosx-version-min=10.13" link=static variant=release threading=multi --build-dir=macOS_arm64_build --prefix=${MY_BOOST_PREFIX} -a install

# remove the boost src and build files
cd ..
rm -r boost_1_85_0.tar.gz boost_1_85_0

# build SpecUtils
cd /path/to/SpecUtils
mkdir build_python
cd build_python
cmake -DSpecUtils_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET="10.13" -DCMAKE_PREFIX_PATH=${MY_BOOST_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64" ..
cmake --build . --config Release -j10


# now, lets test it
mkdir pytest
cd pytest
# We need to rename the shared library to something that will work with Python
cp ../libSpecUtils.dylib ./SpecUtils.so
cp ../../bindings/python/test_python.py .
# The `test_python.py` script uses the `passthrough.n42` file from InterSpec, so
# we need to copy that into the current directory
cp /path/to/InterSpec/example_spectra/passthrough.n42 .

# Now run the test script, making sure to use the same Python we compiled boost against
/opt/homebrew/opt/python3/bin/python3 test_python.py
# You should see some output


# if you want, you can create a proper package (although these instructions may be out of date)
mkdir SpecUtils
cp ../bindings/python/__init__.py ./SpecUtils/
cp libSpecUtils.dylib ./SpecUtils/SpecUtils.so
ln -s ../bindings/python/setup.py .
python3 setup.py bdist_wheel
python3 -m pip install --user --force dist/SpecUtils-0.0.1-cp39-cp39-macosx_12_0_x86_64.whl
```


## Windows build instructions
Assumes you already compiled boost with Python support
```bash
cd SpecUtils
mkdir build_python
cd build_python
cmake -DSpecUtils_PYTHON_BINDINGS=ON -DCMAKE_PREFIX_PATH=/path/to/boost ..
cmake --build . --config Release -j8

mkdir SpecUtils
cp .\Release\libSpecUtils.dll .\SpecUtils\SpecUtils.pyd
cp ..\bindings\python\__init__.py .\SpecUtils\
cp ..\bindings\python\setup.py .
pip install pip setuptools
python.exe -m build --wheel
python.exe -m pip install --user --force .\dist\SpecUtils-0.0.1-cp310-cp310-win_amd64.whl
```