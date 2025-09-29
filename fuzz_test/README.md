# Naive Fuzzing of File Parsing Code

## Prerequisites
- macOS with Homebrew installed
- Boost library compiled and installed (see main SpecUtils README for Boost setup)
- Wt library (optional, for URI spectra support)

## Compiling on macOS (ARM64/Intel)
The default Apple compiler doesn't support the clang fuzzing library, so you need to install LLVM via Homebrew and use specific linking flags to resolve ARM64 compatibility issues.

### Step 1: Install LLVM via Homebrew
```bash
brew install llvm
```

### Step 2: Set up environment variables
```bash
unset CMAKE_OSX_DEPLOYMENT_TARGET

# Since Big Sur v11.1, we need to fix up the LIBRARY_PATH variable
export LIBRARY_PATH="$LIBRARY_PATH:/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib"

# Required for ARM64 fuzzer linking
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -lc++abi"
```

### Step 3: Create build directory and configure
```bash
cd /path/to/SpecUtils
mkdir build_fuzzing
cd build_fuzzing

# Replace /path/to/your/boost/install with your actual Boost installation path
cmake -DCMAKE_BUILD_TYPE="RelWithDebInfo" \
      -DCMAKE_IGNORE_PATH="/Applications/Xcode.app" \
      -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/llvm;/path/to/your/boost/install" \
      -DCMAKE_CXX_COMPILER="/opt/homebrew/opt/llvm/bin/clang++" \
      -DCMAKE_C_COMPILER="/opt/homebrew/opt/llvm/bin/clang" \
      -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
      -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++ -L/opt/homebrew/opt/llvm/lib/c++ -lc++abi" \
      -DSpecUtils_BUILD_FUZZING_TESTS=ON \
      -DSpecUtils_BUILD_REGRESSION_TEST=OFF \
      -DSpecUtils_ENABLE_EQUALITY_CHECKS=ON \
      -DSpecUtils_ENABLE_URI_SPECTRA=ON \
      -DSpecUtils_FLT_PARSE_METHOD=boost \
      ..
```

### Step 4: Build the project
```bash
cmake --build . --config RelWithDebInfo -j8
```

### Troubleshooting
If you encounter linking errors with `std::__1::__hash_memory` symbols, ensure you have:
1. LLVM installed via Homebrew (not just Xcode command line tools)
2. The correct LDFLAGS and CMAKE_EXE_LINKER_FLAGS set as shown above
3. Both `-stdlib=libc++` and `-lc++abi` linking flags specified

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