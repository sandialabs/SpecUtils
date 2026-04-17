using SpecUtils;

if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: SpecUtils.Example <input-file> [output-n42-file]");
    Console.Error.WriteLine();
    Console.Error.WriteLine("  Parses a spectrum file and prints information about it.");
    Console.Error.WriteLine("  If an output path is given and doesn't already exist,");
    Console.Error.WriteLine("  converts the input file to N42-2012 format.");
    return 1;
}

string inputPath = args[0];
string? outputPath = args.Length >= 2 ? args[1] : null;

if (!File.Exists(inputPath))
{
    Console.Error.WriteLine($"Error: Input file not found: {inputPath}");
    return 1;
}

using var specFile = new SpecFile();
bool loaded = specFile.LoadFile(inputPath);
if (!loaded)
{
    Console.Error.WriteLine($"Error: Failed to parse spectrum file: {inputPath}");
    return 1;
}

// ---- File-level information ----
Console.WriteLine($"File: {inputPath}");

string filename = specFile.Filename;
if (!string.IsNullOrEmpty(filename))
    Console.WriteLine($"  Stored filename:  {filename}");

string uuid = specFile.UUID;
if (!string.IsNullOrEmpty(uuid))
    Console.WriteLine($"  UUID:             {uuid}");

Console.WriteLine($"  Detector type:    {specFile.DetectorType}");

string manufacturer = specFile.Manufacturer;
if (!string.IsNullOrEmpty(manufacturer))
    Console.WriteLine($"  Manufacturer:     {manufacturer}");

string model = specFile.InstrumentModel;
if (!string.IsNullOrEmpty(model))
    Console.WriteLine($"  Model:            {model}");

string instrumentId = specFile.InstrumentId;
if (!string.IsNullOrEmpty(instrumentId))
    Console.WriteLine($"  Serial number:    {instrumentId}");

string instrumentType = specFile.InstrumentType;
if (!string.IsNullOrEmpty(instrumentType))
    Console.WriteLine($"  Instrument type:  {instrumentType}");

Console.WriteLine($"  Measurements:     {specFile.NumberMeasurements}");
Console.WriteLine($"  Samples:          {specFile.NumberSamples}");
Console.WriteLine($"  Passthrough:      {(specFile.IsPassthrough ? "Yes" : "No")}");
Console.WriteLine($"  Gamma live time:  {specFile.GammaLiveTimeSum:F2} s");
Console.WriteLine($"  Gamma real time:  {specFile.GammaRealTimeSum:F2} s");
Console.WriteLine($"  Gamma count sum:  {specFile.GammaCountSum:F0}");
Console.WriteLine($"  Neutron count sum:{specFile.NeutronCountSum:F0}");

// Detectors
uint numDet = specFile.NumberDetectors;
if (numDet > 0)
{
    Console.Write("  Detectors:        ");
    for (int i = 0; i < numDet; i++)
    {
        if (i > 0) Console.Write(", ");
        Console.Write($"'{specFile.GetDetectorName(i)}'");
    }
    Console.WriteLine();
}

// Sample numbers
uint numSamples = specFile.NumberSamples;
if (numSamples > 0)
{
    Console.Write("  Sample numbers:   ");
    int maxPrint = (int)Math.Min(numSamples, 20);
    for (int i = 0; i < maxPrint; i++)
    {
        if (i > 0) Console.Write(", ");
        Console.Write(specFile.GetSampleNumber(i));
    }
    if (numSamples > 20)
        Console.Write($", ... ({numSamples} total)");
    Console.WriteLine();
}

// GPS
if (specFile.HasGpsInfo)
    Console.WriteLine($"  GPS:              lat={specFile.MeanLatitude:F6}, lon={specFile.MeanLongitude:F6}");

// Remarks
uint numRemarks = specFile.NumberRemarks;
for (int i = 0; i < numRemarks; i++)
    Console.WriteLine($"  Remark:           {specFile.GetRemark(i)}");

// Parse warnings
uint numWarnings = specFile.NumberParseWarnings;
for (int i = 0; i < numWarnings; i++)
    Console.WriteLine($"  Warning:          {specFile.GetParseWarning(i)}");

// ---- Per-measurement information ----
Console.WriteLine();
Console.WriteLine("Measurements:");

uint numMeas = specFile.NumberMeasurements;
for (int i = 0; i < numMeas; i++)
{
    var meas = specFile.GetMeasurement(i);
    if (meas == null) continue;

    Console.Write($"  [{i}] sample={meas.SampleNumber}, det='{meas.DetectorName}'");
    Console.Write($", live={meas.LiveTime:F2}s, real={meas.RealTime:F2}s");
    Console.Write($", channels={meas.NumberGammaChannels}");
    Console.Write($", gamma_sum={meas.GammaCountSum:F0}");

    if (meas.ContainedNeutron)
        Console.Write($", neutrons={meas.NeutronCountSum:F0}");

    var cal = meas.GetEnergyCalibration();
    if (cal != null && cal.IsValid)
        Console.Write($", energy=[{cal.LowerEnergy:F1}, {cal.UpperEnergy:F1}] keV");

    string title = meas.Title;
    if (!string.IsNullOrEmpty(title))
        Console.Write($", title='{title}'");

    Console.WriteLine();
}

// ---- Output conversion ----
if (outputPath != null)
{
    if (File.Exists(outputPath))
    {
        Console.Error.WriteLine($"\nError: Output file already exists: {outputPath}");
        return 1;
    }

    bool written = specFile.WriteToFile(outputPath, SaveSpectrumAsType.N42_2012);
    if (written)
        Console.WriteLine($"\nConverted to N42-2012: {outputPath}");
    else
    {
        Console.Error.WriteLine($"\nError: Failed to write output file: {outputPath}");
        return 1;
    }
}

return 0;
