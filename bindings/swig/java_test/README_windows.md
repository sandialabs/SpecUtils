# SpecUtils Java Bindings — Windows x86_64

Java bindings for [SpecUtils](https://github.com/sandialabs/SpecUtils), a library for
parsing, manipulating, and exporting gamma spectrum files (N42, SPE, SPC, PCF, CSV, and 100+ other formats).

Built for Windows x86_64 with MSVC.

## Contents

| File | Description |
|------|-------------|
| `SpecUtils.jar` | Java wrapper classes (`gov.sandia.specutils` package) |
| `SpecUtilsJni.dll` | Native JNI shared library |
| `SpecUtilsCli.class` | Example command-line program |
| `SpecUtilsCli.bat` | Launcher script for the CLI example |
| `LICENSE.txt` | LGPL v2.1 license |

## Prerequisites

- Java 8 or newer (JRE to run, JDK to compile your own code)
- Microsoft Visual C++ Redistributable (usually already installed)

## Quick Start — CLI Example

The included CLI tool prints info about a spectrum file, and can optionally convert it to N42-2012:

```cmd
REM Inspect a spectrum file
SpecUtilsCli.bat spectrum.n42

REM Convert to N42-2012 (will not overwrite an existing file)
SpecUtilsCli.bat input.spc output.n42
```

Or run directly with java:
```cmd
java -Djava.library.path=. -classpath ".;SpecUtils.jar" SpecUtilsCli spectrum.n42
```

## Using the Library in Your Project

1. Add `SpecUtils.jar` to your classpath.
2. Place `SpecUtilsJni.dll` somewhere on your library path (e.g., the application directory).
3. In your Java code:

```java
import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.ParserType;
import gov.sandia.specutils.Measurement;

public class MyApp {
    static { System.loadLibrary("SpecUtilsJni"); }

    public static void main(String[] args) {
        SpecFile spec = new SpecFile();
        if (!spec.load_file("input.n42", ParserType.Auto, "")) {
            System.err.println("Failed to load file");
            return;
        }

        System.out.println("Measurements: " + spec.num_measurements());
        System.out.println("Total gamma:  " + spec.gamma_count_sum());

        Measurement m = spec.measurement(0);
        System.out.println("Channels: " + m.gamma_counts().size());
        System.out.println("Live time: " + m.live_time() + "s");
    }
}
```

Compile and run:
```cmd
javac -classpath ".;SpecUtils.jar" MyApp.java
java -Djava.library.path=. -classpath ".;SpecUtils.jar" MyApp
```
