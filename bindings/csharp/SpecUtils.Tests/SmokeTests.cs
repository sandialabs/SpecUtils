using Xunit;
using SpecUtils;

namespace SpecUtils.Tests;

public class SmokeTests
{
    private static string TestDataPath(string filename)
    {
        // Walk up from the bin output directory to find the repo root,
        // then reference test files from bindings/python/examples/
        string dir = AppContext.BaseDirectory;
        while (dir != null && !File.Exists(Path.Combine(dir, "CMakeLists.txt")))
            dir = Path.GetDirectoryName(dir)!;

        if (dir == null)
            throw new InvalidOperationException("Could not find repo root (CMakeLists.txt)");

        return Path.Combine(dir, "bindings", "python", "examples", filename);
    }

    [Fact]
    public void LoadFile_ReturnsNonZeroMeasurements()
    {
        using var spec = new SpecFile();
        bool loaded = spec.LoadFile(TestDataPath("passthrough.n42"));
        Assert.True(loaded);
        Assert.True(spec.NumberMeasurements > 0);
    }

    [Fact]
    public void MeasurementProperties_AreValid()
    {
        using var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));

        var meas = spec.GetMeasurement(0);
        Assert.NotNull(meas);
        Assert.True(meas.NumberGammaChannels > 0);
        Assert.True(meas.LiveTime > 0);
        Assert.True(meas.RealTime > 0);
        Assert.True(meas.GammaCountSum > 0);
        Assert.NotNull(meas.DetectorName);
    }

    [Fact]
    public void GammaChannelCounts_MatchSum()
    {
        using var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));

        var meas = spec.GetMeasurement(0);
        Assert.NotNull(meas);

        float[]? counts = meas.GetGammaChannelCounts();
        Assert.NotNull(counts);
        Assert.Equal((int)meas.NumberGammaChannels, counts.Length);

        double sum = 0;
        foreach (float c in counts)
            sum += c;
        Assert.Equal(meas.GammaCountSum, sum, precision: 0);
    }

    [Fact]
    public void FileProperties_AreAccessible()
    {
        using var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));

        // These should return non-null strings (may be empty)
        Assert.NotNull(spec.Filename);
        Assert.NotNull(spec.UUID);
        Assert.NotNull(spec.Manufacturer);
        Assert.NotNull(spec.InstrumentModel);
        Assert.NotNull(spec.InstrumentId);

        Assert.True(spec.NumberDetectors > 0);
        string detName = spec.GetDetectorName(0);
        Assert.NotNull(detName);

        Assert.True(spec.NumberSamples > 0);
    }

    [Fact]
    public void WriteAndRereadN42_PreservesData()
    {
        using var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));

        uint origMeasCount = spec.NumberMeasurements;
        double origGammaSum = spec.GammaCountSum;

        string tempPath = Path.Combine(Path.GetTempPath(), $"specutils_test_{Guid.NewGuid()}.n42");
        try
        {
            bool written = spec.WriteToFile(tempPath, SaveSpectrumAsType.N42_2012);
            Assert.True(written);
            Assert.True(File.Exists(tempPath));

            using var reread = new SpecFile();
            bool reloaded = reread.LoadFile(tempPath);
            Assert.True(reloaded);

            Assert.Equal(origMeasCount, reread.NumberMeasurements);
            Assert.Equal(origGammaSum, reread.GammaCountSum, precision: 0);
        }
        finally
        {
            if (File.Exists(tempPath))
                File.Delete(tempPath);
        }
    }

    [Fact]
    public void EnergyCalibration_IsValid()
    {
        using var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));

        var meas = spec.GetMeasurement(0);
        Assert.NotNull(meas);

        var cal = meas.GetEnergyCalibration();
        Assert.NotNull(cal);
        Assert.True(cal.IsValid);
        Assert.True(cal.NumberChannels > 0);
        Assert.Equal(meas.NumberGammaChannels, cal.NumberChannels);
        Assert.True(cal.UpperEnergy > cal.LowerEnergy);
    }

    [Fact]
    public void Dispose_PreventsSubsequentUse()
    {
        var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));
        spec.Dispose();

        Assert.Throws<ObjectDisposedException>(() => _ = spec.NumberMeasurements);
        Assert.Throws<ObjectDisposedException>(() => spec.LoadFile("nonexistent.n42"));
    }

    [Fact]
    public void CreateMeasurement_RoundTrips()
    {
        using var meas = new Measurement();
        float[] counts = [1.0f, 2.0f, 3.0f, 4.0f, 5.0f];
        meas.SetGammaCounts(counts, liveTime: 10.0f, realTime: 12.0f);

        Assert.Equal((uint)5, meas.NumberGammaChannels);
        Assert.Equal(10.0f, meas.LiveTime);
        Assert.Equal(12.0f, meas.RealTime);
        Assert.Equal(15.0, meas.GammaCountSum, precision: 0);

        float[]? readBack = meas.GetGammaChannelCounts();
        Assert.NotNull(readBack);
        Assert.Equal(counts.Length, readBack.Length);
        for (int i = 0; i < counts.Length; i++)
            Assert.Equal(counts[i], readBack[i]);
    }

    [Fact]
    public void EnergyCalibration_SetAndQuery()
    {
        var cal = new EnergyCalibration();
        float[] coeffs = [0.0f, 3000.0f];
        bool valid = cal.SetFullRangeFraction(1024, coeffs);
        Assert.True(valid);
        Assert.True(cal.IsValid);
        Assert.Equal((uint)1024, cal.NumberChannels);
        Assert.True(cal.UpperEnergy > 0);

        double midChannel = cal.ChannelForEnergy(1500.0);
        Assert.True(midChannel > 0);

        double midEnergy = cal.EnergyForChannel(512.0);
        Assert.True(midEnergy > 0);

        // Cleanup via CountedRef
        using var countedRef = CountedRefEnergyCalibration.FromEnergyCalibration(cal);
        var borrowed = countedRef.GetEnergyCalibration();
        Assert.NotNull(borrowed);
        Assert.True(borrowed.IsValid);
    }

    [Fact]
    public void GetMeasurementBySampleDet_Works()
    {
        using var spec = new SpecFile();
        spec.LoadFile(TestDataPath("passthrough.n42"));

        if (spec.NumberMeasurements == 0)
            return;

        var first = spec.GetMeasurement(0);
        Assert.NotNull(first);

        int sampleNum = first.SampleNumber;
        string detName = first.DetectorName;

        var byKey = spec.GetMeasurement(sampleNum, detName);
        Assert.NotNull(byKey);
        Assert.Equal(sampleNum, byKey.SampleNumber);
        Assert.Equal(detName, byKey.DetectorName);
    }
}
