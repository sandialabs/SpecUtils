using System.Runtime.InteropServices;

namespace SpecUtils;

/// <summary>
/// Represents a parsed spectrum file containing one or more measurements.
/// Wraps the native SpecUtils::SpecFile C++ class via the C binding layer.
/// </summary>
public class SpecFile : IDisposable
{
    internal IntPtr Handle;
    private bool _disposed;

    /// <summary>Creates a new empty SpecFile.</summary>
    public SpecFile()
    {
        Handle = NativeMethods.SpecUtils_SpecFile_create();
    }

    private SpecFile(IntPtr handle)
    {
        Handle = handle;
    }

    /// <summary>Creates a deep copy of this SpecFile.</summary>
    public SpecFile Clone()
    {
        ThrowIfDisposed();
        IntPtr clone = NativeMethods.SpecUtils_SpecFile_clone(Handle);
        return new SpecFile(clone);
    }

    /// <summary>
    /// Parses the specified spectrum file. The format is auto-detected.
    /// </summary>
    /// <returns>True if the file was successfully parsed.</returns>
    public bool LoadFile(string filename)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecFile_load_file(Handle, filename);
    }

    /// <summary>
    /// Parses the specified spectrum file using the given format.
    /// </summary>
    public bool LoadFile(string filename, ParserType type)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecFile_load_file_from_format(Handle, filename, type);
    }

    /// <summary>
    /// Writes the spectrum file to disk in the specified format.
    /// </summary>
    public bool WriteToFile(string filename, SaveSpectrumAsType type)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_write_to_file(Handle, filename, type);
    }

    /// <summary>Resets to the initial empty state.</summary>
    public void Reset()
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_SpecFile_reset(Handle);
    }

    /// <summary>
    /// Performs cleanup/bookkeeping after adding or removing measurements.
    /// </summary>
    public void Cleanup(bool dontChangeSampleNumbers = false, bool reorderByTime = false)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_SpecFile_cleanup(Handle, dontChangeSampleNumbers, reorderByTime);
    }

    // ---- Properties ----

    public uint NumberMeasurements
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_measurements(Handle); }
    }

    public uint NumberGammaChannels
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_gamma_channels(Handle); }
    }

    public uint NumberDetectors
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_detectors(Handle); }
    }

    public uint NumberGammaDetectors
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_gamma_detectors(Handle); }
    }

    public uint NumberNeutronDetectors
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_neutron_detectors(Handle); }
    }

    public uint NumberSamples
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_samples(Handle); }
    }

    public bool IsPassthrough
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_passthrough(Handle); }
    }

    public bool IsModified
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_modified(Handle); }
    }

    public bool ContainsDerivedData
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_contains_derived_data(Handle); }
    }

    public bool ContainsNonDerivedData
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_contains_non_derived_data(Handle); }
    }

    public float GammaLiveTimeSum
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_sum_gamma_live_time(Handle); }
    }

    public float GammaRealTimeSum
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_sum_gamma_real_time(Handle); }
    }

    public double GammaCountSum
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_gamma_count_sum(Handle); }
    }

    public double NeutronCountSum
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_neutron_counts_sum(Handle); }
    }

    public string Filename
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_filename(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_filename(Handle, value); }
    }

    public string UUID
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_uuid(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_uuid(Handle, value); }
    }

    public string MeasurementLocationName
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_measurement_location_name(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_measurement_location_name(Handle, value); }
    }

    public string MeasurementOperator
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_measurement_operator(Handle)) ?? ""; }
    }

    public DetectorType DetectorType
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_detector_type(Handle); }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_detector_type(Handle, value); }
    }

    public string InstrumentType
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_instrument_type(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_instrument_type(Handle, value); }
    }

    public string Manufacturer
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_manufacturer(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_manufacturer(Handle, value); }
    }

    public string InstrumentModel
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_instrument_model(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_instrument_model(Handle, value); }
    }

    public string InstrumentId
    {
        get { ThrowIfDisposed(); return Marshal.PtrToStringUTF8(NativeMethods.SpecUtils_SpecFile_instrument_id(Handle)) ?? ""; }
        set { ThrowIfDisposed(); NativeMethods.SpecUtils_SpecFile_set_instrument_id(Handle, value); }
    }

    public bool HasGpsInfo
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_has_gps_info(Handle); }
    }

    public double MeanLatitude
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_mean_latitude(Handle); }
    }

    public double MeanLongitude
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_mean_longitude(Handle); }
    }

    public uint MemorySize
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_memmorysize(Handle); }
    }

    // ---- Indexed accessors ----

    /// <summary>
    /// Gets the measurement at the specified index.
    /// The returned Measurement is borrowed and must not outlive this SpecFile.
    /// </summary>
    public Measurement? GetMeasurement(int index)
    {
        ThrowIfDisposed();
        IntPtr ptr = NativeMethods.SpecUtils_SpecFile_get_measurement_by_index(Handle, (uint)index);
        return ptr == IntPtr.Zero ? null : new Measurement(ptr, ownsHandle: false, parent: this);
    }

    /// <summary>
    /// Gets the measurement for the given sample number and detector name.
    /// </summary>
    public Measurement? GetMeasurement(int sampleNumber, string detectorName)
    {
        ThrowIfDisposed();
        IntPtr ptr = NativeMethods.SpecUtils_SpecFile_get_measurement_by_sample_det(
            Handle, sampleNumber, detectorName);
        return ptr == IntPtr.Zero ? null : new Measurement(ptr, ownsHandle: false, parent: this);
    }

    public string GetDetectorName(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_SpecFile_detector_name(Handle, (uint)index)) ?? "";
    }

    public string GetGammaDetectorName(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_SpecFile_gamma_detector_name(Handle, (uint)index)) ?? "";
    }

    public string GetNeutronDetectorName(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_SpecFile_neutron_detector_name(Handle, (uint)index)) ?? "";
    }

    public int GetSampleNumber(int index)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_sample_number(Handle, (uint)index);
    }

    // ---- Remarks and warnings ----

    public uint NumberRemarks
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_remarks(Handle); }
    }

    public string GetRemark(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_SpecFile_remark(Handle, (uint)index)) ?? "";
    }

    public uint NumberParseWarnings
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_SpecFile_number_parse_warnings(Handle); }
    }

    public string GetParseWarning(int index)
    {
        ThrowIfDisposed();
        return Marshal.PtrToStringUTF8(
            NativeMethods.SpecUtils_SpecFile_parse_warning(Handle, (uint)index)) ?? "";
    }

    public void AddRemark(string remark)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_SpecFile_add_remark(Handle, remark);
    }

    // ---- Setters ----

    public void SetLaneNumber(int num)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_SpecFile_set_lane_number(Handle, num);
    }

    public void SetInspection(string inspectionType)
    {
        ThrowIfDisposed();
        NativeMethods.SpecUtils_SpecFile_set_inspection(Handle, inspectionType);
    }

    public bool ChangeDetectorName(string originalName, string newName)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_change_detector_name(Handle, originalName, newName);
    }

    // ---- Measurement management ----

    /// <summary>
    /// Adds a measurement to the file. Ownership of the measurement is transferred to the SpecFile.
    /// After calling this, do not dispose or further modify the Measurement directly.
    /// </summary>
    public bool AddMeasurement(Measurement measurement, bool doCleanup = true)
    {
        ThrowIfDisposed();
        bool result = NativeMethods.SpecUtils_SpecFile_add_measurement(
            Handle, measurement.Handle, doCleanup);
        if (result)
            measurement.TransferOwnership();
        return result;
    }

    // ---- Measurement-level setters (for measurements owned by this SpecFile) ----

    public bool SetMeasurementLiveTime(Measurement measurement, float liveTime)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_live_time(Handle, liveTime, measurement.Handle);
    }

    public bool SetMeasurementRealTime(Measurement measurement, float realTime)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_real_time(Handle, realTime, measurement.Handle);
    }

    public bool SetMeasurementStartTime(Measurement measurement, long microsecondsSinceEpoch)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_start_time(Handle, microsecondsSinceEpoch, measurement.Handle);
    }

    public bool SetMeasurementSourceType(Measurement measurement, SourceType type)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_source_type(Handle, type, measurement.Handle);
    }

    public bool SetMeasurementPosition(Measurement measurement, double longitude, double latitude, long microsecondsSinceEpoch = 0)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_position(Handle, longitude, latitude, microsecondsSinceEpoch, measurement.Handle);
    }

    public bool SetMeasurementTitle(Measurement measurement, string title)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_title(Handle, title, measurement.Handle);
    }

    public bool SetMeasurementContainedNeutrons(Measurement measurement, bool contained, float counts, float neutronLiveTime)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_contained_neutrons(Handle, contained, counts, neutronLiveTime, measurement.Handle);
    }

    public bool SetMeasurementEnergyCalibration(Measurement measurement, CountedRefEnergyCalibration energyCal)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_SpecFile_set_measurement_energy_calibration(Handle, energyCal.Handle, measurement.Handle);
    }

    // ---- IDisposable ----

    internal void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!_disposed)
        {
            if (Handle != IntPtr.Zero)
            {
                NativeMethods.SpecUtils_SpecFile_destroy(Handle);
                Handle = IntPtr.Zero;
            }
            _disposed = true;
        }
    }

    ~SpecFile()
    {
        Dispose(false);
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }
}
