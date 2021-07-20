# Niave Fuzzing of File Parsing Code

## Compiling on macOS
The default Apple compiler doesnt seem to dome with the clang fuzzing library, so you need to install `llvm` and use it to compile the code.  The commands to do this that work for me are:

```bash
brew install llvm
unset CMAKE_OSX_DEPLOYMENT_TARGET
cmake -DCMAKE_IGNORE_PATH="/Applications/Xcode.app" -DCMAKE_PREFIX_PATH="/usr/local/opt/llvm;/path/to/compiled/boost/" -DCMAKE_CXX_COMPILER="/usr/local/opt/llvm/bin/clang++" -DCMAKE_C_COMPILER="/usr/local/opt/llvm/bin/clang" -DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES="/usr/local/opt/llvm/include/c++/v1" -DSpecUtils_BUILD_FUZZING_TESTS=ON ..
make -j16
```

You then need to create a `CORPUS_DIR` that contains a wide variety of sample spectrum files.  
Once you do this, you can run a fuzz job, use a command like:
```bash
# Fuzz for 5 minutes with max file size of 2.5 MB, using 8 different processes 
#  (using -workers=16 argument doesnt seem to cause significantly more cpu use than a single worker)
./fuzz_test/file_parse_fuzz CORPUS_DIR -max_len=2621440 -jobs=8 -print_final_stats=1 -rss_limit_mb=4096 -max_total_time=300
```


## Further information
See https://llvm.org/docs/LibFuzzer.html for 

# Potential Future work
- The fuzzing is probably really niave and could be made way more effective
- Figure out memory limit to use, with an argument like '-rss_limit_mb=64' 
- Add fuzzing for all the string, datetime, and filesystem utilities
- Could target each type of spectrum file parser using candidate files targeted specifically to them, instead of trying each parser for each input
- Also try writing output files whenever the parsing was successful


