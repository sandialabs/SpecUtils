# SpecUtils C# Bindings

C# wrapper for the [SpecUtils](https://github.com/sandialabs/SpecUtils) gamma spectrum file library.

## How it works

The C# library uses [P/Invoke](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/pinvoke) to call SpecUtils's C binding layer (`libSpecUtils` shared library). The C API exposes opaque pointer types that the C# classes wrap with `IDisposable` for automatic memory management.

The key classes are:

- **`SpecFile`** - Parses spectrum files and provides access to measurements, metadata, and file export.
- **`Measurement`** - Represents a single spectrum measurement with gamma channel data, live/real time, detector info, and energy calibration.
- **`EnergyCalibration`** - Represents the mapping between channel numbers and energies (polynomial, full-range-fraction, or lower-channel-edge).

SpecUtils supports reading ~25 spectrum file formats and writing ~14 formats, including N42-2012, PCF, CHN, SPC, and more.

## Building

### 1. Build the native library

```bash
cmake -B build \
  -DSpecUtils_SHARED_LIB=ON \
  -DSpecUtils_C_BINDINGS=ON \
  -DSpecUtils_ENABLE_URI_SPECTRA=ON \
  -DSpecUtils_FLT_PARSE_METHOD=FastFloat \
  -DSpecUtils_FETCH_FAST_FLOAT=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
```

### 2. Set the native library path

```bash
# Point to the directory containing libSpecUtils.{so,dylib,dll}
export SPECUTILS_NATIVE_LIB_DIR=$(pwd)/build
```

### 3. Build and run the C# projects

```bash
dotnet build bindings/csharp/SpecUtils.sln
dotnet test bindings/csharp/SpecUtils.sln
dotnet run --project bindings/csharp/SpecUtils.Example -- path/to/spectrum.n42
```

## Usage example

```csharp
using SpecUtils;

// Load a spectrum file (format auto-detected)
using var specFile = new SpecFile();
bool loaded = specFile.LoadFile("measurement.n42");

Console.WriteLine($"Measurements: {specFile.NumberMeasurements}");
Console.WriteLine($"Detectors:    {specFile.NumberDetectors}");
Console.WriteLine($"Gamma sum:    {specFile.GammaCountSum:F0}");

// Access individual measurements
for (int i = 0; i < specFile.NumberMeasurements; i++)
{
    var meas = specFile.GetMeasurement(i);
    Console.WriteLine($"  [{i}] det='{meas.DetectorName}', " +
        $"channels={meas.NumberGammaChannels}, " +
        $"live={meas.LiveTime:F1}s, " +
        $"counts={meas.GammaCountSum:F0}");

    // Get gamma channel counts
    float[]? counts = meas.GetGammaChannelCounts();

    // Get energy calibration
    var cal = meas.GetEnergyCalibration();
    if (cal != null && cal.IsValid)
        Console.WriteLine($"    Energy range: {cal.LowerEnergy:F1} - {cal.UpperEnergy:F1} keV");
}

// Save to a different format
specFile.WriteToFile("output.n42", SaveSpectrumAsType.N42_2012);
```

## Requirements

- .NET 10.0 or later
- Native `libSpecUtils` shared library (built from the SpecUtils C++ source)
