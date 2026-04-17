using System.Runtime.InteropServices;

namespace SpecUtils;

internal static partial class NativeMethods
{
    private const string LibName = "libSpecUtils";
    private const CallingConvention CC = CallingConvention.Cdecl;

    // ---- SpecFile lifecycle ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_create();

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_clone(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_destroy(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_equal(IntPtr lhs, IntPtr rhs);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_reset(IntPtr instance);

    // ---- SpecFile parsing ----

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecFile_load_file(
        IntPtr instance,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecFile_load_file_from_format(
        IntPtr instance,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        ParserType type);

    // ---- SpecFile writing ----

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_write_to_file(
        IntPtr instance,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string filename,
        SaveSpectrumAsType type);

    // ---- SpecFile properties ----

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_passthrough(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_measurements(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_gamma_channels(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_get_measurement_by_index(
        IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_get_measurement_by_sample_det(
        IntPtr instance,
        int sampleNumber,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string detName);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_detectors(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_detector_name(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_gamma_detectors(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_gamma_detector_name(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_neutron_detectors(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_neutron_detector_name(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_samples(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern int SpecUtils_SpecFile_sample_number(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_SpecFile_sum_gamma_live_time(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_SpecFile_sum_gamma_real_time(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_SpecFile_gamma_count_sum(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_SpecFile_neutron_counts_sum(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_filename(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_uuid(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_measurement_location_name(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_measurement_operator(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern DetectorType SpecUtils_SpecFile_detector_type(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_instrument_type(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_manufacturer(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_instrument_model(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_instrument_id(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_has_gps_info(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_SpecFile_mean_latitude(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_SpecFile_mean_longitude(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_contains_derived_data(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_contains_non_derived_data(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_modified(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_remarks(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_remark(IntPtr instance, uint remarkIndex);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_number_parse_warnings(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_parse_warning(IntPtr instance, uint warningIndex);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_SpecFile_memmorysize(IntPtr instance);

    // ---- SpecFile sum measurements ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_SpecFile_sum_measurements(
        IntPtr instance,
        int[] sampleNumbers, uint numSamples,
        IntPtr[] detectorNames, uint numDetectors);

    // ---- SpecFile measurement management ----

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_add_measurement(
        IntPtr instance, IntPtr measurement,
        [MarshalAs(UnmanagedType.U1)] bool doCleanup);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_remove_measurement(
        IntPtr instance, IntPtr measurement,
        [MarshalAs(UnmanagedType.U1)] bool doCleanup);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_cleanup(
        IntPtr instance,
        [MarshalAs(UnmanagedType.U1)] bool dontChangeSampleNumbers,
        [MarshalAs(UnmanagedType.U1)] bool reorderByTime);

    // ---- SpecFile setters ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_filename(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string filename);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_uuid(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string uuid);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_add_remark(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string remark);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_lane_number(IntPtr instance, int num);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_measurement_location_name(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string locationName);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_inspection(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string inspectionType);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_instrument_type(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string instrumentType);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_detector_type(
        IntPtr instance, DetectorType type);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_manufacturer(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string manufacturer);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_instrument_model(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string model);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_SpecFile_set_instrument_id(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string serialNumber);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_change_detector_name(
        IntPtr instance,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string originalName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string newName);

    // ---- SpecFile measurement setters ----

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_live_time(
        IntPtr instance, float liveTime, IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_real_time(
        IntPtr instance, float realTime, IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_start_time(
        IntPtr instance, long microsecondsSinceEpoch, IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_source_type(
        IntPtr instance, SourceType type, IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_position(
        IntPtr instance, double longitude, double latitude,
        long microsecondsSinceEpoch, IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_title(
        IntPtr instance,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string title,
        IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_contained_neutrons(
        IntPtr instance,
        [MarshalAs(UnmanagedType.U1)] bool contained,
        float counts, float neutronLiveTime,
        IntPtr measurement);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_SpecFile_set_measurement_energy_calibration(
        IntPtr instance, IntPtr energyCal, IntPtr measurement);

    // ---- Measurement lifecycle ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_create();

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_clone(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_destroy(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_equal(IntPtr lhs, IntPtr rhs);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_reset(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_Measurement_memmorysize(IntPtr instance);

    // ---- Measurement properties ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_description(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_source_string(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_title(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern long SpecUtils_Measurement_start_time_usecs(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern byte SpecUtils_Measurement_pcf_tag(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_Measurement_number_gamma_channels(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_gamma_channel_counts(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_energy_bounds(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_energy_calibration(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_energy_calibration_ref(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_Measurement_real_time(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_Measurement_live_time(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_Measurement_neutron_live_time(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_Measurement_gamma_count_sum(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_Measurement_neutron_count_sum(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_is_occupied(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_contained_neutron(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern int SpecUtils_Measurement_sample_number(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_detector_name(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_Measurement_speed(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern OccupancyStatus SpecUtils_Measurement_occupancy_status(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_has_gps_info(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_Measurement_latitude(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_Measurement_longitude(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern long SpecUtils_Measurement_position_time_microsec(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_Measurement_dose_rate(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_Measurement_exposure_rate(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_detector_type(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern QualityStatus SpecUtils_Measurement_quality_status(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern SourceType SpecUtils_Measurement_source_type(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_Measurement_number_remarks(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_remark(IntPtr instance, uint remarkIndex);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_Measurement_number_parse_warnings(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_Measurement_parse_warning(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_Measurement_gamma_integral(
        IntPtr instance, float lowerEnergy, float upperEnergy);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_Measurement_gamma_channels_sum(
        IntPtr instance, uint startBin, uint endBin);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_Measurement_derived_data_properties(IntPtr instance);

    // ---- Measurement setters ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_description(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string description);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_source_string(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceString);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_gamma_counts(
        IntPtr instance, float[] counts, uint nchannels, float liveTime, float realTime);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_neutron_counts(
        IntPtr instance, float[] counts, uint numTubes, float neutronLiveTime);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_title(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string title);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_start_time_usecs(
        IntPtr instance, long startTime);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_set_start_time_str(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string startTimeStr);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_pcf_tag(IntPtr instance, byte tagChar);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_sample_number(IntPtr instance, int sampleNum);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_detector_name(
        IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_occupancy_status(
        IntPtr instance, OccupancyStatus status);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_position(
        IntPtr instance, double longitude, double latitude, long positionTimeMicrosec);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_Measurement_set_source_type(
        IntPtr instance, SourceType type);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_combine_gamma_channels(
        IntPtr instance, uint nchannel);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_rebin(IntPtr instance, IntPtr cal);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_Measurement_set_energy_calibration(
        IntPtr instance, IntPtr cal);

    // ---- EnergyCalibration lifecycle ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_EnergyCal_create();

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_EnergyCal_destroy(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_CountedRef_EnergyCal_create();

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern void SpecUtils_CountedRef_EnergyCal_destroy(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_EnergyCal_ptr_from_ref(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_EnergyCal_make_counted_ref(IntPtr instance);

    // ---- EnergyCalibration properties ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern EnergyCalType SpecUtils_EnergyCal_type(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_EnergyCal_valid(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_EnergyCal_number_coefficients(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_EnergyCal_coefficients(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_EnergyCal_number_deviation_pairs(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_EnergyCal_deviation_energy(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_EnergyCal_deviation_offset(IntPtr instance, uint index);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern uint SpecUtils_EnergyCal_number_channels(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern IntPtr SpecUtils_EnergyCal_channel_energies(IntPtr instance);

    // ---- EnergyCalibration setters ----

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_EnergyCal_set_polynomial(
        IntPtr instance, uint numChannels,
        float[] coeffs, uint numCoeffs,
        float[]? devPairs, uint numDevPairs);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_EnergyCal_set_full_range_fraction(
        IntPtr instance, uint numChannels,
        float[] coeffs, uint numCoeffs,
        float[]? devPairs, uint numDevPairs);

    [DllImport(LibName, CallingConvention = CC)]
    [return: MarshalAs(UnmanagedType.U1)]
    internal static extern bool SpecUtils_EnergyCal_set_lower_channel_energy(
        IntPtr instance, uint numChannels,
        uint numEnergies, float[] channelEnergies);

    // ---- EnergyCalibration utilities ----

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_EnergyCal_channel_for_energy(IntPtr instance, double energy);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern double SpecUtils_EnergyCal_energy_for_channel(IntPtr instance, double channel);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_EnergyCal_lower_energy(IntPtr instance);

    [DllImport(LibName, CallingConvention = CC)]
    internal static extern float SpecUtils_EnergyCal_upper_energy(IntPtr instance);
}
