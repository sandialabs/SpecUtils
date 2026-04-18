# SpecUtils Java SWIG Bindings

## Prerequisites

You need the Java JDK, SWIG, and CMake installed and in your environment variables.

On macOS, using Homebrew, you could install these with commands like:

```bash
brew update
brew upgrade
brew install swig
brew install openjdk

# Set the needed environment variables:
export JAVA_HOME=$(brew --prefix openjdk)
export PATH="$JAVA_HOME/bin:$PATH"

# And/or add to your .zshrc file for the future
echo 'export JAVA_HOME=$(brew --prefix openjdk)' >> ~/.zshrc
echo 'export PATH="$JAVA_HOME/bin:$PATH"' >> ~/.zshrc

# Test JDK is installed
javac -version
```

## Building

To compile the Java bindings:
```bash
cd SpecUtils/bindings/swig
mkdir build
cd build

# Configure the project with CMake
cmake -DSpecUtils_FLT_PARSE_METHOD=strtod ..

# If the configuration failed with issues finding the Java stuff, you can
# either edit the CMakeLists.txt file and hardcode some commented out path lines,
# or preferably, explicitly specify the java paths to cmake, like:
# cmake -DJAVA_INCLUDE_PATH=".../include" -DJAVA_JVM_LIBRARY=".../libjvm.dylib" -DSpecUtils_FLT_PARSE_METHOD=strtod ..

make -j8
```

This will produce:
- A native JNI library (`libSpecUtilsJni.jnilib` on macOS, `libSpecUtilsJni.so` on Linux, `SpecUtilsJni.dll` on Windows)
- `SpecUtils.jar` containing the Java wrapper classes in the `gov.sandia.specutils` package


## Running the Test

From the build directory:
```bash
cp ../java_test/TestSpecUtils.java .
javac -classpath .:SpecUtils.jar TestSpecUtils.java
java -Djava.library.path="." -classpath .:SpecUtils.jar TestSpecUtils [optional_spectrum_file]
```


## Running the CLI Example

From the build directory:
```bash
cp ../java_test/SpecUtilsCli.java .
javac -classpath .:SpecUtils.jar SpecUtilsCli.java

# Inspect a spectrum file
java -Djava.library.path="." -classpath .:SpecUtils.jar SpecUtilsCli some_spectrum.n42

# Convert to N42-2012 (will not overwrite an existing file)
java -Djava.library.path="." -classpath .:SpecUtils.jar SpecUtilsCli input.spc output.n42
```


## Running the Example GUI Application

To run the example Java GUI application (requires JFreeChart):
```bash
cp ../java_example/* .
javac -classpath .:SpecUtils.jar:jcommon-1.0.21.jar:jfreechart-1.0.17.jar:joda-time-2.9.jar *.java gov/sandia/specutils/*.java
java -Djava.library.path="." -classpath .:SpecUtils.jar:jcommon-1.0.21.jar:jfreechart-1.0.17.jar:joda-time-2.9.jar Main
```

## Using in Your Project

To use SpecUtils in a Java project:

1. Add `SpecUtils.jar` to your classpath
2. Place the native library (`libSpecUtilsJni.jnilib`/`.so`/`.dll`) where Java can find it
3. Load the native library in your code:

```java
System.loadLibrary("SpecUtilsJni");
```

4. Import and use the classes:

```java
import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.ParserType;
import gov.sandia.specutils.Measurement;

SpecFile specFile = new SpecFile();
specFile.load_file("spectrum.n42", ParserType.Auto, "");
long numMeasurements = specFile.num_measurements();
```
