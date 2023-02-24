# Niave Fuzzing of File Parsing Code

## Compiling on macOS
The default Apple compiler doesnt seem to dome with the clang fuzzing library, so you need to install `llvm` and use it to compile the code.  The commands to do this that work for me are:

```bash
brew install llvm

unset CMAKE_OSX_DEPLOYMENT_TARGET

# Since Big Sur v11.1, we need to fix up the LIBRARY_PATH variable
export LIBRARY_PATH="$LIBRARY_PATH:/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib"  

cmake -DCMAKE_IGNORE_PATH="/Applications/Xcode.app" -DCMAKE_PREFIX_PATH="/usr/local/opt/llvm;/path/to/compiled/boost/" -DCMAKE_CXX_COMPILER="/usr/local/opt/llvm/bin/clang++" -DCMAKE_C_COMPILER="/usr/local/opt/llvm/bin/clang" -DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES="/usr/local/opt/llvm/include/c++/v1" -DSpecUtils_BUILD_FUZZING_TESTS=ON ..

cmake --build . --config RelWithDebInfo -j16
```

You then need to create a `CORPUS_DIR` that contains a wide variety of sample spectrum files.  
Once you do this, you can run a fuzz job, use a command like:
```bash
# Fuzz for 5 minutes with max file size of 2.5 MB, using 8 different processes 
#  (using -workers=16 argument doesnt seem to cause significantly more cpu use than a single worker)
./fuzz_test/file_parse_fuzz CORPUS_DIR -max_len=2621440 -jobs=8 -print_final_stats=1 -rss_limit_mb=4096 -max_total_time=300


./fuzz_test/fuzz_str_utils
```

Since files are written to your CORPUS_DIR, you probably want to make a copy of the directory, before running.


## Further information
See https://llvm.org/docs/LibFuzzer.html for 

# Potential Future work
- The fuzzing is probably really niave and could be made way more effective
- Figure out memory limit to use, with an argument like '-rss_limit_mb=64' 
- Add fuzzing for all the string, datetime, and filesystem utilities
- Could target each type of spectrum file parser using candidate files targeted specifically to them, instead of trying each parser for each input
- Also try writing output files whenever the parsing was successful


# Static Analysis
Its also pretty easy to run static analysis over the code.

## CppCheck
In your build directory, run something like the following commands.  
This will then cause CMake to run `cppcheck` over each source file when you build things, giving you the error messages
```bash
cd path/to/build/dir
cmake "-DCMAKE_CXX_CPPCHECK:FILEPATH=/usr/local/bin/cppcheck;--suppress=*:/path/to/boost/include/boost/config.hpp;--suppress=*:*:/path/to/boost/include/boost/include/boost/config/compiler/codegear.hpp;--force;--std=c++11" ..
make clean
make
```

## Clang / scan-build
To run clangs static analysis, use the following commands:
```bash
cd /path/to/SpecUtils
export PATH=/usr/local/opt/llvm/bin/:$PATH
scan-build --use-cc=/usr/local/opt/llvm/bin/clang --use-c++=/usr/local/opt/llvm/bin/clang++ make -C build_dir
```