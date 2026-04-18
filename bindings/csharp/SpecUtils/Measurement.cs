using System.Runtime.InteropServices;

namespace SpecUtils;

/// <summary>
/// Represents a single spectrum measurement/record within a SpecFile.
/// May be owned (caller must dispose) or borrowed (lifetime tied to parent SpecFile).
/// </summary>
public class Measurement : IDisposable
{
    internal IntPtr Handle;
    private bool _ownsHandle;
    private bool _disposed;

    // Hold a reference to parent SpecFile to prevent GC when we have borrowed pointers
    private readonly SpecFile? _parent;

    /// <summary>Creates a new empty owned Measurement.</summary>
    public Measurement()
    {
        Handle = NativeMethods.SpecUtils_Measurement_create();
        _ownsHandle = true;
    }

    internal Measurement(IntPtr handle, bool ownsHandle, SpecFile? parent = null)
    {
        Handle = handle;
        _ownsHandle = ownsHandle;
        _parent = parent;
    }

    /// <summary>Creates a deep copy (owned by caller).</summary>
    public Measurement Clone()
    {
        ThrowIfDisposed();
        IntPtr clone = NativeMethods.SpecUtils_Measurement_clone(Handle);
        return new Measurement(clone, ownsHandle: true);
    }

    /// <summary>Resets to initial empty state. Only valid on owned measurements.</summary>
    public void Reset()
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_Measurement_reset(Handle);
    }

    /// <summary>Called when ownership is transferred to a SpecFile.</summary>
    internal void TransferOwnership()
    {
        _ownsHandle = false;
    }

    // ---- Properties ----

    public string Description
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_Measurement_description(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_description(Handle, value); }
    }

    public string SourceString
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_Measurement_source_string(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_source_string(Handle, value); }
    }

    public string Title
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_Measurement_title(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_title(Handle, value); }
    }

    public long StartTimeMicroseconds
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_start_time_usecs(Handle); }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_start_time_usecs(Handle, value); }
    }

    public char PcfTag
    {
        get { ThrowIfDisposed(); return (char)NativeMethods.SpecUtils_Measurement_pcf_tag(Handle); }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_pcf_tag(Handle, (byte)value); }
    }

    public uint NumberGammaChannels
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_number_gamma_channels(Handle); }
    }

    public float RealTime
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_real_time(Handle); }
    }

    public float LiveTime
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_live_time(Handle); }
    }

    public float NeutronLiveTime
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_neutron_live_time(Handle); }
    }

    public double GammaCountSum
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_gamma_count_sum(Handle); }
    }

    public double NeutronCountSum
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_neutron_count_sum(Handle); }
    }

    public bool IsOccupied
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_is_occupied(Handle); }
    }

    public bool ContainedNeutron
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_contained_neutron(Handle); }
    }

    public int SampleNumber
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_sample_number(Handle); }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_sample_number(Handle, value); }
    }

    public string DetectorName
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_Measurement_detector_name(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_detector_name(Handle, value); }
    }

    public float Speed
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_speed(Handle); }
    }

    public OccupancyStatus OccupancyStatus
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_occupancy_status(Handle); }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_occupancy_status(Handle, value); }
    }

    public bool HasGpsInfo
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_has_gps_info(Handle); }
    }

    public double Latitude
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_latitude(Handle); }
    }

    public double Longitude
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_longitude(Handle); }
    }

    public long PositionTimeMicroseconds
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_position_time_microsec(Handle); }
    }

    public float DoseRate
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_dose_rate(Handle); }
    }

    public float ExposureRate
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_exposure_rate(Handle); }
    }

    public string DetectorTypeName
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_Measurement_detector_type(Handle)) ?? ""; }
    }

    public QualityStatus QualityStatus
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_quality_status(Handle); }
    }

    public SourceType SourceType
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_source_type(Handle); }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_Measurement_set_source_type(Handle, value); }
    }

    public uint MemorySize
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_memmorysize(Handle); }
    }

    public uint DerivedDataProperties
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_derived_data_properties(Handle); }
    }

    // ---- Remarks and warnings ----

    public uint NumberRemarks
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_number_remarks(Handle); }
    }

    public string GetRemark(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_Measurement_remark(Handle, (uint)index)) ?? "";
    }

    public uint NumberParseWarnings
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_Measurement_number_parse_warnings(Handle); }
    }

    public string GetParseWarning(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_Measurement_parse_warning(Handle, (uint)index)) ?? "";
    }

    // ---- Data access ----

    /// <summary>
    /// Returns a copy of the gamma channel counts as a float array.
    /// Returns null if no gamma data is present.
    /// </summary>
    public float[]? GetGammaChannelCounts()
    {
        ThrowIfDisposed();
        uint nChannels = NativeMethods.SpecUtils_Measurement_number_gamma_channels(Handle);
        if (nChannels == 0)
            return null;

        IntPtr ptr = NativeMethods.SpecUtils_Measurement_gamma_channel_counts(Handle);
        if (ptr == IntPtr.Zero)
            return null;

        float[] counts = new float[nChannels];
        Marshal.Copy(ptr, counts, 0, (int)nChannels);
        return counts;
    }

    /// <summary>
    /// Returns a copy of the energy bounds (lower channel energies) as a float array.
    /// The array has NumberGammaChannels + 1 entries.
    /// Returns null if no energy calibration is set.
    /// </summary>
    public float[]? GetEnergyBounds()
    {
        ThrowIfDisposed();
        uint nChannels = NativeMethods.SpecUtils_Measurement_number_gamma_channels(Handle);
        if (nChannels == 0)
            return null;

        IntPtr ptr = NativeMethods.SpecUtils_Measurement_energy_bounds(Handle);
        if (ptr == IntPtr.Zero)
            return null;

        float[] energies = new float[nChannels + 1];
        Marshal.Copy(ptr, energies, 0, (int)(nChannels + 1));
        return energies;
    }

    /// <summary>
    /// Gets the energy calibration for this measurement (borrowed, do not dispose).
    /// </summary>
    public EnergyCalibration? GetEnergyCalibration()
    {
        ThrowIfDisposed();
        IntPtr ptr = NativeMethods.SpecUtils_Measurement_energy_calibration(Handle);
        return ptr == IntPtr.Zero ? null : new EnergyCalibration(ptr, ownsHandle: false);
    }

    /// <summary>
    /// Returns the gamma integral between the specified energies.
    /// </summary>
    public double GammaIntegral(float lowerEnergy, float upperEnergy)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_Measurement_gamma_integral(Handle, lowerEnergy, upperEnergy);
    }

    /// <summary>
    /// Returns the sum of gamma counts between the specified bin indices.
    /// </summary>
    public double GammaChannelsSum(uint startBin, uint endBin)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_Measurement_gamma_channels_sum(Handle, startBin, endBin);
    }

    // ---- Setters ----

    /// <summary>Sets gamma channel counts, live time, and real time.</summary>
    public void SetGammaCounts(float[] counts, float liveTime, float realTime)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_Measurement_set_gamma_counts(
            Handle, counts, (uint)counts.Length, liveTime, realTime);
    }

    /// <summary>Sets neutron counts from one or more tubes.</summary>
    public void SetNeutronCounts(float[] counts, float neutronLiveTime)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_Measurement_set_neutron_counts(
            Handle, counts, (uint)counts.Length, neutronLiveTime);
    }

    public bool SetStartTimeString(string startTimeStr)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_Measurement_set_start_time_str(Handle, startTimeStr);
    }

    public void SetPosition(double longitude, double latitude, long positionTimeMicrosec = 0)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_Measurement_set_position(Handle, longitude, latitude, positionTimeMicrosec);
    }

    public bool CombineGammaChannels(uint nchannels)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_Measurement_combine_gamma_channels(Handle, nchannels);
    }

    public bool Rebin(CountedRefEnergyCalibration cal)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_Measurement_rebin(Handle, cal.Handle);
    }

    public bool SetEnergyCalibration(CountedRefEnergyCalibration cal)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_Measurement_set_energy_calibration(Handle, cal.Handle);
    }

    // ---- IDisposable ----

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!_disposed)
        {
            if (_ownsHandle && Handle != IntPtr.Zero)
            {
                NativeMethods.SpecUtils_Measurement_destroy(Handle);
            }
            Handle = IntPtr.Zero;
            _disposed = true;
        }
    }

    ~Measurement()
    {
        Dispose(false);
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }
}
