# Prerequisites

You need the Java JDK and Swig packages installed and in your environment variables.

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


To compile with support for Java use the following commands:
```bash
cd SpecUtils/bindings/swig
mkdir build
cd build

# Configure the project with CMake
cmake ..

# If the configuration failed with issues finding the Java stuff, you can
# either edit the CMakeLists.txt file and hardcode some commented out path lines,
# or prefferably, explicitly specify the java paths to cmake, like:
cmake -DJAVA_INCLUDE_PATH=".../include" -DJAVA_JVM_LIBRARY=".../libjvm.dylib" ..

make -j8
```

All the .java files will then be in `gov/sandia/specutils`

To run the example Java executable, you can:
```bash
cp ../java_example/* .
javac -classpath .:jcommon-1.0.21.jar:jfreechart-1.0.17.jar:joda-time-2.9.jar *.java gov/sandia/specutils/*.java
java -Djava.library.path="." -classpath .:jcommon-1.0.21.jar:jfreechart-1.0.17.jar:joda-time-2.9.jar Main
```
