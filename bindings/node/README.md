# Creating a Node Module

To create the add-on we use [cmake-js](https://www.npmjs.com/package/cmake-js) to build things, and [node-addon-api](https://www.npmjs.com/package/node-addon-api) to actually interface the C++ to JS.


```bash
npm install -g cmake-js 

# For macOS only, you may want to define a deployment target
export MACOSX_DEPLOYMENT_TARGET=10.10

cd /path/to/SpecUtils/bindings/node/

# Install dependency for compiling a node.js add-on
npm install --save-dev node-addon-api

# If boost and Wt are in standard locations, you can just run
cmake-js

# Or to have a little more control over things
cmake-js --CDBOOST_ROOT=/path/to/boost \
         --CDCMAKE_BUILD_TYPE="Release" \
         --out="build_dir"

# If you make changes and want to recompile
cmake-js build --out="build_dir"
# Or you can use CMake to run the `make` command, which can be useful when
# the CMake generator isnt a command-line based system like (ex Xcode, MSVC)
cmake --build build_dir --config Release
# Or directly use the `make` command like:
ninja -C build_dir


# And also copy all the InterSpec resources to the 
# 'app' sub-directory of your build dir
cmake-js build --out=build_dir --target install
# Or
cmake --build build_dir --target install --config Release
# Or
ninja -C build_dir install
```


## Linux Considerations
The `InterSpec` module is really a shared library that node.js loads, therefore you need the `-fPIC` C/C++ compiler flag enabled not just for the `InterSpec` code, but for all of the static libraries you link it against, including boost, Wt, and zlib - which isnt the default when compiling static libraries for any of them, so when building them you may want to add `-fPIC -std=c++11` flags to the flags.


## Future Work
