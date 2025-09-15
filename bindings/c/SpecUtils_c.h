#ifndef SpecUtils_c_h
#define SpecUtils_c_h

#include <stdint.h>
#include <stdbool.h>

#ifndef CALLINGCONVENTION
#ifdef _WIN32
    #define CALLINGCONVENTION __cdecl
#else
    #define CALLINGCONVENTION
#endif
#endif //CALLINGCONVENTION

#ifndef DLLEXPORT
#ifdef _WIN32
    #define DLLEXPORT __declspec(dllexport)
#else
    #define DLLEXPORT
#endif
#endif //DLLEXPORT

#ifdef __cplusplus
extern "C" 
{
#endif

typedef struct SpecUtils_SpecFile SpecUtils_SpecFile;
typedef struct SpecUtils_Measurement SpecUtils_Measurement;
  
/** An opaque pointer to a SpecUtils::EnergyCalibration object. */
typedef struct SpecUtils_EnergyCal SpecUtils_EnergyCal;

/** A pointer to a `std::shared_ptr<SpecUtils::EnergyCalibration>` object.
 useful primarily if you are creating a spectrum file, and want multiple Measurements to
 */
typedef struct SpecUtils_CountedRef_EnergyCal SpecUtils_CountedRef_EnergyCal;

  
/** Allocate and initialize a new empty `SpecUtils::SpecFile` object, returning a pointer to it.
 
 Note: You must call `SpecUtils_SpecFile_destroy(instance)` to de-allocate this object to avoid a memory leak.
 */
DLLEXPORT SpecUtils_SpecFile * CALLINGCONVENTION
SpecUtils_SpecFile_create();
  
/** Creates a copy of passed in `SpecUtils_SpecFile`.
 
 Note: You must call `SpecUtils_SpecFile_destroy(instance)` to de-allocate this object to avoid a memory leak.
 */
DLLEXPORT SpecUtils_SpecFile * CALLINGCONVENTION
SpecUtils_SpecFile_clone( const SpecUtils_SpecFile * const instance );
  
/** De-allocates a `SpecUtils::SpecFile` object created using `SpecUtils_SpecFile_create()`.
 */
DLLEXPORT void CALLINGCONVENTION 
SpecUtils_SpecFile_destroy( SpecUtils_SpecFile *instance );
  
  
  /** Sets the `lhs` equal to the `rhs`. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_equal( SpecUtils_SpecFile *lhs, const SpecUtils_SpecFile *rhs );
  
/** Parses the specified spectrum file into the  provided `SpecUtils::SpecFile` instance.
 
 The input spectrum file format will first be guessed based on the provided filename, then if not successful,
 all possible input formats will be tried.
 
 @param instance The `SpecUtils::SpecFile` object to hold the parsed file; must have been created using
        `SpecUtils_SpecFile_create()`, and non NULL.
 @param filename The utf-8 encoded path of the file to parse.
        Note, even on Windows, the path must be UTF-8, and not the local code-point being used.
 @returns `true` if the file was successfully parsed, and there was at least one record, or else, `instance`
        will be reset to the freshly initialized state and `false` returned.
 
 This function is equivalent to calling:
  `SpecFile_load_file_from_format(instance,filename,SpecUtils_Input_Auto)`
 
 */
DLLEXPORT bool CALLINGCONVENTION 
SpecFile_load_file( SpecUtils_SpecFile *instance,
                     const char * const filename );

  
/** A C enum corresponding to the C++ enum class `SpecUtils::ParserType`, to allow parsing a file, from a
 specified spectrum file type.
 */
enum SpecUtils_ParserType
{
  SpecUtils_Parser_N42_2006, SpecUtils_Parser_N42_2012, SpecUtils_Parser_Spc,
  SpecUtils_Parser_Exploranium, SpecUtils_Parser_Pcf, SpecUtils_Parser_Chn, 
  SpecUtils_Parser_SpeIaea, SpecUtils_Parser_TxtOrCsv, SpecUtils_Parser_Cnf,
  SpecUtils_Parser_TracsMps, SpecUtils_Parser_Aram, SpecUtils_Parser_SPMDailyFile,
  SpecUtils_Parser_AmptekMca, SpecUtils_Parser_MicroRaider, SpecUtils_Parser_RadiaCode,
  SpecUtils_Parser_OrtecListMode, SpecUtils_Parser_LsrmSpe, SpecUtils_Parser_Tka,
  SpecUtils_Parser_MultiAct, SpecUtils_Parser_Phd, SpecUtils_Parser_Lzs,
  SpecUtils_Parser_ScanDataXml, SpecUtils_Parser_Json, SpecUtils_Parser_CaenHexagonGXml,
  #if( SpecUtils_ENABLE_URI_SPECTRA )
  SpecUtils_Parser_Uri,
  #endif
  SpecUtils_Parser_Auto
};//enum SpecUtils_ParserType
  
/** Loads spectrum file from only the spectrum file format specified.  i.e., if input file is any-other format
 than it wont load.
 
 Specifying the input format should not normally be necessary to do, but may be used if `SpecFile_load_file(...)`
 is somehow not properly detecting the input format (which should not happen), or the filename doesnt hint at the
 file type, and you dont want to waste the CPU time figuring it out from the file contents.
 */
DLLEXPORT bool CALLINGCONVENTION 
SpecFile_load_file_from_format( SpecUtils_SpecFile * const instance,
                                const char * const filename,
                               const enum SpecUtils_ParserType type );
  
  
/** A C enum corresponding to the C++ enum class `SpecUtils::SaveSpectrumAsType`.
 */
enum SpecUtils_SaveSpectrumAsType
{
  SpecUtils_SaveAsTxt,
  SpecUtils_SaveAsCsv,
  SpecUtils_SaveAsPcf,
  SpecUtils_SaveAsN42_2006,
  SpecUtils_SaveAsN42_2012,
  SpecUtils_SaveAsChn,
  SpecUtils_SaveAsSpcBinaryInt,
  SpecUtils_SaveAsSpcBinaryFloat,
  SpecUtils_SaveAsSpcAscii,
  SpecUtils_SaveAsExploraniumGr130v0,
  SpecUtils_SaveAsExploraniumGr135v2,
  SpecUtils_SaveAsSpeIaea,
  SpecUtils_SaveAsCnf,
  SpecUtils_SaveAsTka,
    
  #if( SpecUtils_ENABLE_D3_CHART )
  SpecUtils_SaveAsHtmlD3,
  #endif
    
  #if( SpecUtils_INJA_TEMPLATES )
  SpecUtils_SaveAsTemplate,
  #endif
    
  #if( SpecUtils_ENABLE_URI_SPECTRA )
  SpecUtils_SaveAsUri,
  #endif

  SpecUtils_SaveAsNumTypes
};//enum SpecUtils_SaveSpectrumAsType
  
/** Writes the contents of the provided `SpecUtils_SpecFile` to a spectrum file.
 
 @param instance The `SpecUtils_SpecFile` to write to file.
 @param filename The UTF-8 encoded file path to write to.
        Note, even on Windows, the path must be UTF-8, and not the local code-point being used.
 @param type The spectrum file format to write to
 @returns if writing was successful or not.  If not successful, if the output file exists, or its contents
        are undefined, so on failure you should check if the file specified by `filename` exists,
        and delete it.
 */
DLLEXPORT bool CALLINGCONVENTION 
SpecUtils_write_to_file( SpecUtils_SpecFile *instance,
                           const char *filename,
                        enum SpecUtils_SaveSpectrumAsType type );

/** Returns if the spectrum file is likely a portal or search mode system.
 
 E.g. a system that measurements at regular intervals, where it would make sense to plot a time-history chart
 */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_passthrough( const SpecUtils_SpecFile * const instance );

  
/** Returns the number of `SpecUtils_Measurement` measurements held by the specified `SpecUtils_SpecFile`.
 */
DLLEXPORT uint32_t CALLINGCONVENTION 
SpecUtils_SpecFile_number_measurements( const SpecUtils_SpecFile * const instance );
  
/** Return the maximum number of gamma channels of any `SpecUtils_Measurement` owned by the provided
 `SpecUtils_SpecFile` instance.
 
 Note: individual measurements may have different numbers of channels.
 */
DLLEXPORT uint32_t CALLINGCONVENTION 
SpecUtils_SpecFile_number_gamma_channels( const SpecUtils_SpecFile * const instance );
  
/** Returns pointer to the measurement at the specified index.
 
 @param instance The `SpecUtils_SpecFile` to access.
 @param index The index of the measurement to return, should be from 0 to `SpecUtils_SpecFile_num_gamma_channels(instance) -1`.
 @returns pointer to specified measurement - or NULL if index is to large.
 */
DLLEXPORT const SpecUtils_Measurement* CALLINGCONVENTION
SpecUtils_SpecFile_get_measurement_by_index( const SpecUtils_SpecFile * const instance,
                                     const uint32_t index );
  
/** Returns pointer to the measurement with the specified sample number and detector name.
 
 The combination of sample number and detector name normally uniquely specifies a specific measurement.
 
 @param instance The `SpecUtils_SpecFile` to access.
 @param sample_number The sample number
 @returns The specified measurement.  Will be `NULL`if the specified sample number for the specified detector
          doesnt exist.
 
 \sa
 */
DLLEXPORT const SpecUtils_Measurement* CALLINGCONVENTION
SpecUtils_SpecFile_get_measurement_by_sample_det( const SpecUtils_SpecFile * const instance,
                                          const int sample_number,
                                          const char * const det_name );
  
/** Returns the number of detectors (both gamma and/or neutron) in the spectrum file.
 
 The data for each detector is referenced using the detectors name, and usually
 defined by the input spectrum file.
 */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_number_detectors( const SpecUtils_SpecFile * const instance );

/** Returns the detector name, for the specified index.
 
 Ex., "Aa1", "Ab1N", "VD1", "gamma", "GT", "Det.1", etc.
 
 @param instance The `SpecUtils_SpecFile` to access.
 @param index Inclusively between 0 and `SpecUtils_SpecFile_number_detectors(instance) - 1`.
 @returns Null-terminated string giving the detector name.  Will be `NULL` if invalid index.
 
 \sa SpecUtils_SpecFile_number_detectors
 */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_detector_name( const SpecUtils_SpecFile * const instance,
                                 const uint32_t index );

/** Returns the number of detectors who provide any gamma spectra. */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_number_gamma_detectors( const SpecUtils_SpecFile * const instance );

/** Returns the gamma detector name, for the specified index.
   
  @param instance The `SpecUtils_SpecFile` to access.
  @param index Inclusively between 0 and `SpecUtils_SpecFile_number_gamma_detectors(instance) - 1`.
  @returns Null-terminated string giving the detector name.  Will be `NULL` if invalid index.
 
 \sa SpecUtils_SpecFile_number_gamma_detectors
*/
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_gamma_detector_name( const SpecUtils_SpecFile * const instance,
                                   const uint32_t index );
  
/** Same as `SpecUtils_SpecFile_number_gamma_detectors`, but for neutrons */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_number_neutron_detectors( const SpecUtils_SpecFile * const instance );

/** Same as `SpecUtils_SpecFile_gamma_detector_name`, but for neutrons */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_neutron_detector_name( const SpecUtils_SpecFile * const instance,
                                     const uint32_t index );
  
/** Returns the number of samples in the spectrum file.
 
 Normally a specific sample groups all measurements made at the same time together.
 E.g., if a detection system has 8 detectors, and each one records a spectrum each 1 second,
 than a given sample number will provide all 8 measurements made during the same second.
 
 The sample numbers are usually monotonically increasing, but may start at -1, 0, or 1.
 However, there is no requirement the sample numbers are sequential, and some spectrum
 files will skip sample numbers, so you will need to call
 `SpecUtils_SpecFile_sample_number(instance,index)`
 to retrieve the actual sample number to use.
 
 In principle, and usually, all measurements of a given sample will have the same start time, and
 same real time, however, there are some detection systems that will have different real times
 (if this is the case, the most common deviation from normal is the neutron detector(s) having
 a different real time than gamma detectors).
 */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_number_samples( const SpecUtils_SpecFile * const instance );
  
/** Returns the sample number, for a given index.
 
 @param instance The `SpecUtils_SpecFile` to access.
 @param index Inclusively between 0 and `SpecUtils_SpecFile_number_samples(instance) - 1`.
 @returns The sample number for the given index.  If index is invalid, returns `INT_MIN`
 */
DLLEXPORT int CALLINGCONVENTION
SpecUtils_SpecFile_sample_number( const SpecUtils_SpecFile * const instance, const uint32_t index );

/** Adds a measurement to the specified spectrum file.
 
 @param instance The `SpecUtils_SpecFile` to add the measurement to.
 @param measurement The measurement to add.  The `SpecUtils_SpecFile` will take ownership
        of this measurement.  The measurement must have been created by `SpecUtils_Measurement_create()`,
        and then must NOT be de-allocated by `SpecUtils_Measurement_destroy()`.
 @param do_cleanup If specified true, then all the sums and detector names and sample numbers will be
        re-calculated, and any pointers to detector names, sample number index, etc will be invalidated.
        If false, then things wont be updated, but you MUST call `SpecUtils_SpecFile_cleanup(instance)`
        before accessing any measurements, detector names, sample numbers, etc.
        Using a value of `false` is computationally advantageous if adding many measurements.
 @returns If operation was successful.  Will be true, unless input pointers are `NULL`.
 
 After calling this function, any pointers to detector names, or sample numbers, etc will be invalidated.
 */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_add_measurement( SpecUtils_SpecFile * instance,
                SpecUtils_Measurement *measurement,
                const bool do_cleanup );
  
/** Removes a measurement from the spectrum file.
 
 After removing the measurement, you own the `SpecUtils_Measurement`, so you must call
 `SpecUtils_Measurement_destroy(measurement)` afterwards, in order to not leak memory.
 
 @param instance The `SpecUtils_SpecFile` to remove the measurement from.
 @param measurement The measurement to remove.  Must be owned by the `instance`.
 @param do_cleanup If true, all sums and such are recalculated.  If false, you MUST call
        `SpecUtils_SpecFile_cleanup(instance)` before accessing any measurement, sample number, etc.
        Using a value of `false` is computationally advantageous if removing many measurements.
 @returns If measurement was successfully removed (i.e., the measurement was originally owned by the
          spectrum file).  If true, the measurement may have been deallocated, and is no longer safe
          to access (you should NOT destroy).
 
 After calling this function, any pointers to detector names, or sample numbers, etc will be invalidated.
 */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_remove_measurement( SpecUtils_SpecFile * instance,
                   const SpecUtils_Measurement * const measurement,
                   const bool do_cleanup );

/** Removes many `SpecUtils_Measurement` from the spectrum file.
 
 The cleanup function will be called after removing the measurements.
 You must call `SpecUtils_Measurement_destroy(measurement)` for each removed
 measurement, after this call, or memory will be leaked.
 
 @param instance The `SpecUtils_SpecFile` to remove the measurements from.
 @param measurements The array of measurements to remove.
 @param number_to_remove The number of measurements in `measurements` to remove.
 @returns If all measurements were owned by the spectrum file, and removed them.
          If any measurement passed in was not owned by the spectrum file, then no measurement
          will not be removed, and false will be returned.
 
 After calling this function, any pointers to detector names, or sample numbers, etc will be invalidated.
 */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_remove_measurements( SpecUtils_SpecFile * instance,
                    const SpecUtils_Measurement ** const measurements,
                    const uint32_t number_to_remove );

  
/** Resets the spectrum file to its initially empty state. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_reset( SpecUtils_SpecFile * instance );

/** Cleans up the spectrum file, after adding measurements, recalculating sums, assigning sample
 numbers, and performing internal book-keeping necessary to access information.
 
 @param instance The `SpecUtils_SpecFile` to to cleanup.
 @param dont_change_sample_numbers If true, then the added measurements wont have sample
        numbers changed, or measurements re-ordered.  You normally will specify this as false.
 @param reorder_by_time If true, then measurements and sample numbers will be set by the
        start time of each measurement.  If true, will over-ride `dont_change_sample_numbers`.
 
 After calling this function, any pointers to detector names, or sample numbers, etc will be invalidated.
 */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_cleanup( SpecUtils_SpecFile * instance,
                                const bool dont_change_sample_numbers,
                                const bool reorder_by_time );

/** Returns if the spectrum file has been modified since it was loaded, or
 `SpecUtils_SpecFile_cleanup(...)` last called
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_modified( const SpecUtils_SpecFile * const instance );
  
/** Sums the spectra and neutron counts of the specified measurements.
 
 @param instance The `SpecUtils_SpecFile` to to cleanup.
 @param sample_numbers An array of sample numbers to sum over.
 @param number_sample_numbers The number of entries in `sample_numbers` to use.
 @param detector_names An array of strings holding the detector names to sum.
 @param number_detector_names The number of entries in `detector_names` to use.
 @returns a `SpecUtils_Measurement` you own, that is the sum over the specified sample
        number and detector names.  You must call `SpecUtils_Measurement_destroy(measurement)`
        or else there will be a memory leak.
        Will return `NULL` if any sample numbers of detector names are invalid, or no valid measurements
        are specified.
 */
DLLEXPORT SpecUtils_Measurement * CALLINGCONVENTION
SpecUtils_SpecFile_sum_measurements( const SpecUtils_SpecFile * const instance,
                                    const int * const sample_numbers,
                                    const uint32_t number_sample_numbers,
                                    const char ** const detector_names,
                                    const uint32_t number_detector_names );

/** Returns the approximate number of bytes this spectrum file takes up in memory. */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_memmorysize( const SpecUtils_SpecFile * const instance );
  

/** Returns the number of file-level remarks the spectrum file contained. */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_number_remarks( const SpecUtils_SpecFile * const instance );

/** Returns null-terminated strings of file-level remarks in the file.
 
 Will return `NULL` only in requested index is too large.
 */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_remark( const SpecUtils_SpecFile * const instance, 
                          const uint32_t remark_index );
  
/** Returns number of warnings generated during spectrum file parsing. */
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_SpecFile_number_parse_warnings( const SpecUtils_SpecFile * const instance );

/** Returns individual parse warnings. */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_parse_warning( const SpecUtils_SpecFile * const instance,
                                 const uint32_t warning_index  );
  
  
DLLEXPORT float CALLINGCONVENTION
SpecUtils_SpecFile_sum_gamma_live_time( const SpecUtils_SpecFile * const instance );

DLLEXPORT float CALLINGCONVENTION
SpecUtils_SpecFile_sum_gamma_real_time( const SpecUtils_SpecFile * const instance );

DLLEXPORT double CALLINGCONVENTION
SpecUtils_SpecFile_gamma_count_sum( const SpecUtils_SpecFile * const instance );

DLLEXPORT double CALLINGCONVENTION
SpecUtils_SpecFile_neutron_counts_sum( const SpecUtils_SpecFile * const instance );
  
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_filename( const SpecUtils_SpecFile * const instance );

DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_uuid( const SpecUtils_SpecFile * const instance );
  
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_measurement_location_name( const SpecUtils_SpecFile * const instance );
    
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_measurement_operator( const SpecUtils_SpecFile * const instance );

/** The C equivalent of `SpecUtils::DetectorType`.
 
 This enum provides the detected or inferred detection system that created this spectrum file.
 */
enum SpecUtils_DetectorType
{
  SpecUtils_Det_Exploranium, SpecUtils_Det_IdentiFinder, SpecUtils_Det_IdentiFinderNG,
  SpecUtils_Det_IdentiFinderLaBr3, SpecUtils_Det_IdentiFinderTungsten,
  SpecUtils_Det_IdentiFinderR425NaI, SpecUtils_Det_IdentiFinderR425LaBr,
  SpecUtils_Det_IdentiFinderR500NaI, SpecUtils_Det_IdentiFinderR500LaBr,
  SpecUtils_Det_IdentiFinderUnknown, SpecUtils_Det_DetectiveUnknown,
  SpecUtils_Det_DetectiveEx, SpecUtils_Det_DetectiveEx100, SpecUtils_Det_DetectiveEx200,
  SpecUtils_Det_DetectiveX, SpecUtils_Det_SAIC8, SpecUtils_Det_Falcon5000,
  SpecUtils_Det_MicroDetective, SpecUtils_Det_MicroRaider,
  SpecUtils_Det_RadiaCodeCsI10, SpecUtils_Det_RadiaCodeCsI14, SpecUtils_Det_RadiaCodeGAGG10,
  SpecUtils_Det_Raysid,
  SpecUtils_Det_Interceptor, SpecUtils_Det_RadHunterNaI,
  SpecUtils_Det_RadHunterLaBr3, SpecUtils_Det_Rsi701, SpecUtils_Det_Rsi705,
  SpecUtils_Det_AvidRsi, SpecUtils_Det_OrtecRadEagleNai, SpecUtils_Det_OrtecRadEagleCeBr2Inch,
  SpecUtils_Det_OrtecRadEagleCeBr3Inch, SpecUtils_Det_OrtecRadEagleLaBr, SpecUtils_Det_Sam940LaBr3,
  SpecUtils_Det_Sam940, SpecUtils_Det_Sam945, SpecUtils_Det_Srpm210,
  SpecUtils_Det_RIIDEyeNaI, SpecUtils_Det_RIIDEyeLaBr, SpecUtils_Det_RadSeekerNaI,
  SpecUtils_Det_RadSeekerLaBr, SpecUtils_Det_VerifinderNaI, SpecUtils_Det_VerifinderLaBr,
  SpecUtils_Det_KromekD3S, SpecUtils_Det_KromekD5, SpecUtils_Det_KromekGR1,
  SpecUtils_Det_Fulcrum, SpecUtils_Det_Fulcrum40h, SpecUtils_Det_Sam950,
  SpecUtils_Det_Unknown
};//enum SpecUtils_DetectorType
  
  
/** Returns the detection system that created this spectrum file.
 
This may be inferred from spectrum file format or from comments or information within the 
spectrum file.
 */
DLLEXPORT enum SpecUtils_DetectorType CALLINGCONVENTION
SpecUtils_SpecFile_detector_type( const SpecUtils_SpecFile * const instance );

/** Returns the instrument type as specified, or inferred, from the spectrum file.
 
 Examples of returned values are: "Spectrometer", "PortalMonitor", "SpecPortal", "RadionuclideIdentifier", 
 "PersonalRadiationDetector", "SurveyMeter", "Other", etc
 */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_instrument_type( const SpecUtils_SpecFile * const instance );

/** The instrument manufacturer specified in, or inferred from, the spectrum file */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_manufacturer( const SpecUtils_SpecFile * const instance );

/** The instrument model specified in, or inferred from, the spectrum file */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_instrument_model( const SpecUtils_SpecFile * const instance );
  
/** The serial number of the instrument that made the spectrum file. */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_instrument_id( const SpecUtils_SpecFile * const instance );
  
  
/** Returns if mean longitude/latitude are valid gps coords */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_has_gps_info( const SpecUtils_SpecFile * const instance );
  
DLLEXPORT double CALLINGCONVENTION
SpecUtils_SpecFile_mean_latitude( const SpecUtils_SpecFile * const instance );
  
DLLEXPORT double CALLINGCONVENTION
SpecUtils_SpecFile_mean_longitude( const SpecUtils_SpecFile * const instance );
    
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_contains_derived_data( const SpecUtils_SpecFile * const instance );
  
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_contains_non_derived_data( const SpecUtils_SpecFile * const instance );
  
  
// simple setters
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_filename( SpecUtils_SpecFile *instance,
                                  const char * const filename );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_remarks( SpecUtils_SpecFile *instance,
                                 const char ** const remarks,
                                 const uint32_t number_remarks );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_add_remark( SpecUtils_SpecFile *instance,
                                const char * const remark );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_parse_warnings( SpecUtils_SpecFile *instance,
                                        const char ** const warnings,
                                        const uint32_t number_warnings );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_uuid( SpecUtils_SpecFile *instance,
                              const char * const file_uuid );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_lane_number( SpecUtils_SpecFile *instance, const int num );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_location_name( SpecUtils_SpecFile *instance,
                                                   const char * const location_name );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_inspection( SpecUtils_SpecFile *instance,
                                    const char * const inspection_type );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_instrument_type( SpecUtils_SpecFile *instance,
                                         const char * const instrument_type );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_detector_type( SpecUtils_SpecFile *instance,
                                     const enum SpecUtils_DetectorType type );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_manufacturer( SpecUtils_SpecFile *instance,
                                      const char * const manufacturer_name );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_instrument_model( SpecUtils_SpecFile *instance,
                                          const char * const model );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_SpecFile_set_instrument_id( SpecUtils_SpecFile *instance,
                                       const char * const serial_number );
  
/** Changes a detectors name.
   
  @returns if rename was successful.
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_change_detector_name( SpecUtils_SpecFile *instance,
                            const char * const original_name,
                            const char * const new_name );
  
/** Sets the spectrum files energy calibration using a .CALp file.
   
  @returns if was successful
*/
DLLEXPORT bool CALLINGCONVENTION
set_energy_calibration_from_CALp_file( SpecUtils_SpecFile *instance,
                                             const char * const CALp_filepath );
  
  
  // Simple setters for Measurements owned by the SpecFile
  
/** Sets the live time for the specified measurement.
   
  Returns if successful (i.e., if measurement was owned by the `SpecUtils_SpecFile`).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_live_time( SpecUtils_SpecFile *instance,
                                   const float live_time,
                                   const SpecUtils_Measurement * const measurement );
  
/** Sets the real time (aka, clock time, absolute time, actual time, etc.) for the specified measurement.
   
  Returns if successful (i.e., if measurement was owned by the `SpecUtils_SpecFile`).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_real_time( SpecUtils_SpecFile *instance,
                                   const float real_time,
                                   const SpecUtils_Measurement * const measurement );
  
/** Sets the start time for the specified measurement.
   
  @param microseconds_since_unix_epoch The number of micro-seconds since the UNIX epoch (i.e., Jan 1 1970)
   
  @returns if successful (i.e., if measurement was owned by the `SpecUtils_SpecFile`).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_start_time( SpecUtils_SpecFile *instance,
                                    const int64_t microseconds_since_unix_epoch,
                                    const SpecUtils_Measurement * const measurement );
  
/** Similar to `SpecUtils_SpecFile_set_start_time(...)`, but you can pass the date/time in as a
  string.  Recommend using VAX (ex "19-Sep-2014 14:12:01.62") or ISO (ex "2014-04-14T14:12:01.621543"), so
  it is not ambiguous.
   
  @returns if successful (valid date/time and measurement is valid).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_start_time_str( SpecUtils_SpecFile *instance,
                                    const char *date_time,
                                    const SpecUtils_Measurement * const measurement );
  
/** Sets the remarks for the specified measurement.
   
  Returns if successful (i.e., if measurement was owned by the `SpecUtils_SpecFile`).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_remarks( SpecUtils_SpecFile *instance,
                                 const char ** const remarks,
                                 const uint32_t number_remarks,
                                 const SpecUtils_Measurement * const measurement );
  
enum SpecUtils_SourceType
{
  SpecUtils_SourceType_IntrinsicActivity, 
  SpecUtils_SourceType_Calibration,
  SpecUtils_SourceType_Background, 
  SpecUtils_SourceType_Foreground,
  SpecUtils_SourceType_Unknown
};
  
/** Sets the source-type of a specific measurement. */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_source_type( SpecUtils_SpecFile *instance,
                                               const enum SpecUtils_SourceType type,
                       const SpecUtils_Measurement * const measurement );
  
/** Sets the GPS coordinates for the measurement
   
  @param microseconds_since_unix_epoch The time of the GPS reading.  Use a value of 0 to indicate not relevant.
   
  Returns if successful (i.e., if measurement was owned by the `SpecUtils_SpecFile`).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_position( SpecUtils_SpecFile *instance,
                    const double longitude,
                    const double latitude,
                    const int64_t microseconds_since_unix_epoch,
                    const SpecUtils_Measurement * const measurement );
  
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_title( SpecUtils_SpecFile * instance,
                 const char * const title,
                 const SpecUtils_Measurement * const measurement );
  
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_contained_neutrons( SpecUtils_SpecFile *instance,
                              const bool contained, const float counts,
                              const float neutron_live_time,
                              const SpecUtils_Measurement * const measurement );

  
  
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_SpecFile_set_measurement_energy_calibration( SpecUtils_SpecFile *instance,
                         SpecUtils_CountedRef_EnergyCal *energy_cal,
                         const SpecUtils_Measurement * const measurement );
  
  
// Measurement
/** Creates a initialized `SpecUtils::Measurement` object. 
 
 You must either add the returned pointer to a `SpecUtils_SpecFile`, or call
 `SpecUtils_Measurement_destroy(instance)` on result to not leak memory.
 
 \sa `SpecUtils_SpecFile_add_measurement`
 */
DLLEXPORT SpecUtils_Measurement * CALLINGCONVENTION
SpecUtils_Measurement_create();

/** Creates a copy of a `SpecUtils::Measurement`.
 
 You must either add the returned pointer to a `SpecUtils_SpecFile`, or call
 `SpecUtils_Measurement_destroy(instance)` on result to not leak memory.
 */
DLLEXPORT SpecUtils_Measurement * CALLINGCONVENTION
SpecUtils_Measurement_clone( const SpecUtils_Measurement * const instance );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_destroy( SpecUtils_Measurement *instance );

DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_Measurement_memmorysize( const SpecUtils_Measurement * const instance );

/** Sets the `lhs` equal to the `rhs`. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_equal( SpecUtils_Measurement *lhs, const SpecUtils_Measurement *rhs );
  
/** Resets the Measurement to the same state as when initially allocated. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_reset( SpecUtils_Measurement *instance );
  
/** Returns the measurement description. Zero terminated, and non-null. */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_description( const SpecUtils_Measurement * const instance );

  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_description( SpecUtils_Measurement *instance,
                                      const char * const description );

/** Returns the measurement description. Zero terminated, and non-null. */
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_source_string( const SpecUtils_Measurement * const instance );
  
/** This is a GADRAS specific function - it actually adds a remark to Measurement that starts with "Source:" */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_source_string( SpecUtils_Measurement *instance,
                                        const char * const source_string );
  
DLLEXPORT void CALLINGCONVENTION 
SpecUtils_Measurement_set_gamma_counts( SpecUtils_Measurement *instance,
                                       const float *counts,
                                       const uint32_t nchannels,
                                       const float live_time,
                                       const float real_time );

/** Sets the neutron counts.
 
 @param instance The `SpecUtils_Measurement` to to modify.
 @param counts The array of counts from each neutron detector.  Most detectors will probably have one entry
        but for example, if you have multiple He3 tubes, you may have multiple entries.
 @param num_tubes The number of entries in `counts`.
 @param neutron_live_time The live time of the neutron measurement.  If it is the same as the gamma detector
        in this measurement, you can provide a negative value, and the gamma real-time will be used.
 */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_neutron_counts( SpecUtils_Measurement *instance,
                                         const float * const counts,
                                         const uint32_t num_tubes,
                                         const float neutron_live_time );
  
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_title( const SpecUtils_Measurement * const instance );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_title( SpecUtils_Measurement *instance,
                                const char * const title );
  
/** Returns number of micro-seconds since the UNIX epoch (i.e., Jan 1, 1970) that this measurement
 started.
 
 A returned value of zero indicates no time available.
 */
DLLEXPORT int64_t CALLINGCONVENTION
SpecUtils_Measurement_start_time_usecs( SpecUtils_Measurement *instance );
  
/** Sets the start time of the measurement, using micro-seconds since the UNIX epoch to define the time. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_start_time_usecs( SpecUtils_Measurement *instance,
                                           const int64_t start_time );
  
/** Similar to `SpecUtils_Measurement_set_start_time_usecs(...)`, but you can pass the date/time in as a
  string.  Recommend using VAX (ex "19-Sep-2014 14:12:01.62") or ISO (ex "2014-04-14T14:12:01.621543"), so
  it is not ambiguous.
     
  @returns if successful (valid date/time string).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_set_start_time_str( SpecUtils_Measurement *instance,
                                            const char * const start_time_str );
  
/** Returns the application-specific "tag" characters used by the PCF file format. */
DLLEXPORT char CALLINGCONVENTION
SpecUtils_Measurement_pcf_tag( const SpecUtils_Measurement * const instance );

/** Sets the application-specific "tag" characters used by the PCF file format. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_pcf_tag( SpecUtils_Measurement *instance,
                                  const char tag_char );
  
DLLEXPORT uint32_t CALLINGCONVENTION 
SpecUtils_Measurement_number_gamma_channels( const SpecUtils_Measurement * const instance );
  
/** Returns array of gamma channel counts, having `SpecUtils_Measurement_number_gamma_channels(instance)`
 entries.
 
 May return `NULL` pointer if no counts.
 */
DLLEXPORT const float * CALLINGCONVENTION
SpecUtils_Measurement_gamma_channel_counts( const SpecUtils_Measurement * const instance );

/** Returns array of lower channel energies. 
 
 Returned array will have one more entry than the number of gamma channels.
 Could potentially return `NULL`.
 
 \sa SpecUtils_Measurement_number_gamma_channels
 */
DLLEXPORT const float * CALLINGCONVENTION
SpecUtils_Measurement_energy_bounds( const SpecUtils_Measurement * const instance );

/** Returns a pointer to the energy calibration for this measurement.
 
 You do NOT own the object pointed to by the returned answer.  
 Do NOT call `SpecUtils_CountedRef_EnergyCal_destroy(instance)`
 or `SpecUtils_EnergyCal_make_counted_ref(instance)` on the returned pointer.
 */
DLLEXPORT const SpecUtils_EnergyCal * CALLINGCONVENTION
SpecUtils_Measurement_energy_calibration( const SpecUtils_Measurement * const instance );
  
/** Returns a pointer to a `shared_ptr<const SpecUtils::EnergyCalibration>` object.
   
 You own the object pointed to by the returned answer - so you need to call
 `SpecUtils_CountedRef_EnergyCal_destroy(instance)`, or else it will be a memory leak.
 
 Returned pointer will be `NULL` if measurement didnt have energy calibration set.
*/
DLLEXPORT const SpecUtils_CountedRef_EnergyCal * CALLINGCONVENTION
SpecUtils_Measurement_energy_calibration_ref( const SpecUtils_Measurement * const instance );
  
  
DLLEXPORT float CALLINGCONVENTION 
SpecUtils_Measurement_real_time( const SpecUtils_Measurement * const instance );
  
DLLEXPORT float CALLINGCONVENTION
SpecUtils_Measurement_live_time( const SpecUtils_Measurement * const instance );
  
DLLEXPORT float CALLINGCONVENTION 
SpecUtils_Measurement_neutron_live_time( const SpecUtils_Measurement * const instance );
  
DLLEXPORT double CALLINGCONVENTION 
SpecUtils_Measurement_gamma_count_sum( const SpecUtils_Measurement * const instance );
  
DLLEXPORT double CALLINGCONVENTION
SpecUtils_Measurement_neutron_count_sum( const SpecUtils_Measurement * const instance );
  
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_is_occupied( const SpecUtils_Measurement * const instance );
    
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_contained_neutron( const SpecUtils_Measurement * const instance );
  
DLLEXPORT int CALLINGCONVENTION
SpecUtils_Measurement_sample_number(const SpecUtils_Measurement * const instance);

DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_sample_number( SpecUtils_Measurement *instance,
                                          const int samplenum );
  
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_detector_name( const SpecUtils_Measurement * const instance );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_detector_name( SpecUtils_Measurement * instance,
                                          const char *name );
  
DLLEXPORT float CALLINGCONVENTION
SpecUtils_Measurement_speed( const SpecUtils_Measurement * const instance );
  
enum SpecUtils_OccupancyStatus
{
  SpecUtils_OccupancyStatus_NotOccupied,
  SpecUtils_OccupancyStatus_Occupied,
  SpecUtils_OccupancyStatus_Unknown
};
  
DLLEXPORT enum SpecUtils_OccupancyStatus CALLINGCONVENTION
SpecUtils_Measurement_occupancy_status( const SpecUtils_Measurement * const instance );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_occupancy_status( SpecUtils_Measurement *instance,
                       const enum SpecUtils_OccupancyStatus status );
  
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_has_gps_info( const SpecUtils_Measurement * const instance );
  
DLLEXPORT double CALLINGCONVENTION
SpecUtils_Measurement_latitude( const SpecUtils_Measurement * const instance );
  
DLLEXPORT double CALLINGCONVENTION
SpecUtils_Measurement_longitude( const SpecUtils_Measurement * const instance );
  
DLLEXPORT int64_t CALLINGCONVENTION
SpecUtils_Measurement_position_time_microsec( const SpecUtils_Measurement * const instance );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_position( SpecUtils_Measurement *instance,
                                     const double longitude,
                                     const double latitude,
                                     const int64_t position_time_microsec );
    
DLLEXPORT float CALLINGCONVENTION
SpecUtils_Measurement_dose_rate( const SpecUtils_Measurement * const instance );
DLLEXPORT float CALLINGCONVENTION
SpecUtils_Measurement_exposure_rate( const SpecUtils_Measurement * const instance );
  
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_detector_type( const SpecUtils_Measurement * const instance );
  
enum SpecUtils_QualityStatus
{
  SpecUtils_QualityStatus_Good,
  SpecUtils_QualityStatus_Suspect,
  SpecUtils_QualityStatus_Bad,
  SpecUtils_QualityStatus_Missing
};

DLLEXPORT enum SpecUtils_QualityStatus CALLINGCONVENTION
SpecUtils_Measurement_quality_status( const SpecUtils_Measurement * const instance );

DLLEXPORT enum SpecUtils_SourceType CALLINGCONVENTION
SpecUtils_Measurement_source_type( const SpecUtils_Measurement * const instance );

DLLEXPORT void
SpecUtils_Measurement_set_source_type( SpecUtils_Measurement *instance,
                                        const enum SpecUtils_SourceType type );
    
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_Measurement_number_remarks( const SpecUtils_Measurement * const instance );
  
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_remark( const SpecUtils_Measurement * const instance,
                               const uint32_t remark_index );
  
DLLEXPORT void CALLINGCONVENTION
SpecUtils_Measurement_set_remarks( SpecUtils_Measurement *instance,
                                    const char **remarks,
                                    const uint32_t number_remarks );
  
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_Measurement_number_parse_warnings( const SpecUtils_Measurement * const instance );
DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_Measurement_parse_warning( const SpecUtils_Measurement * const instance,
                                      const uint32_t remark_index );
    
DLLEXPORT double CALLINGCONVENTION
SpecUtils_Measurement_gamma_integral( const SpecUtils_Measurement * const instance,
                                       const float lower_energy, const float upper_energy );
    
DLLEXPORT double CALLINGCONVENTION
SpecUtils_Measurement_gamma_channels_sum( const SpecUtils_Measurement * const instance,
                                           const uint32_t startbin,
                                           const uint32_t endbin );
  
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_Measurement_derived_data_properties( const SpecUtils_Measurement * const instance );
    
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_combine_gamma_channels( SpecUtils_Measurement *instance,
                                               const uint32_t nchannel );
  
/** Changes the channel energies to match the provided energy calibration.
   
  Does not change the energy of peaks, but does change the channel numbers of peaks, and counts of
  the channels, and possibly even the number of channels.
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_rebin( SpecUtils_Measurement *instance,
                              const SpecUtils_CountedRef_EnergyCal * const cal );
    
/** Sets the energy calibration of the Measurement.
   
  Does not change the channel counts, or channel numbers of peaks, but (may) change the energy of peaks
   
  @returns If the new energy calibration is applied.  Change of calibration may not be applied if the input calibration
            has the wrong number of channels, is invalid, etc
  */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_Measurement_set_energy_calibration( SpecUtils_Measurement *instance,
                                               const SpecUtils_CountedRef_EnergyCal * const cal );
    
  
  
/** Create a SpecUtils::EnergyCalibration object.
  You will need to set calibration coefficients before it will be useful.
   
  You need to either call `SpecUtils_EnergyCal_destroy(instance)` for the returned
  result, or call
*/
DLLEXPORT SpecUtils_EnergyCal * CALLINGCONVENTION
SpecUtils_EnergyCal_create();
    
/** De-allocates a EnergyCalibration. */
DLLEXPORT void CALLINGCONVENTION
SpecUtils_EnergyCal_destroy( SpecUtils_EnergyCal *instance );
    
DLLEXPORT SpecUtils_CountedRef_EnergyCal * CALLINGCONVENTION
SpecUtils_CountedRef_EnergyCal_create();
    
DLLEXPORT void CALLINGCONVENTION
SpecUtils_CountedRef_EnergyCal_destroy( SpecUtils_CountedRef_EnergyCal *instance );

  /** Returns the `SpecUtils_EnergyCal` pointer owned by a `SpecUtils_CountedRef_EnergyCal`
   
   Do NOT call `SpecUtils_EnergyCal_destroy(instance)` on the returned result - its lifetime is managed
   by the counted ref pointer you passed in
   */
DLLEXPORT const SpecUtils_EnergyCal * CALLINGCONVENTION
SpecUtils_EnergyCal_ptr_from_ref( SpecUtils_CountedRef_EnergyCal *instance );
   
   /** Converts the passed in `SpecUtils_EnergyCal` object to a `SpecUtils_CountedRef_EnergyCal`,
    which will then manage the `SpecUtils_EnergyCal` lifetime.
    
    The `SpecUtils_EnergyCal` you pass in must have been created using `SpecUtils_EnergyCal_create()`
    and must not have been passed to this function before.  Also you must NOT call
    `SpecUtils_EnergyCal_destroy(instance)` for the `SpecUtils_EnergyCal` passed in.
    */
DLLEXPORT SpecUtils_CountedRef_EnergyCal * CALLINGCONVENTION
SpecUtils_EnergyCal_make_counted_ref( SpecUtils_EnergyCal *instance );
    
    
    
/** Energy calibration type - please see `SpecUtils::EnergyCalType` for full documentation. */
enum SpecUtils_EnergyCalType
{
  SpecUtils_EnergyCal_Polynomial,
  SpecUtils_EnergyCal_FullRangeFraction,
  SpecUtils_EnergyCal_LowerChannelEdge,
  SpecUtils_EnergyCal_UnspecifiedUsingDefaultPolynomial,
  SpecUtils_EnergyCal_InvalidEquationType
};//enum SpecUtils_EnergyCalType

DLLEXPORT enum SpecUtils_EnergyCalType CALLINGCONVENTION
SpecUtils_EnergyCal_type( const SpecUtils_EnergyCal * const instance );

DLLEXPORT bool CALLINGCONVENTION
SpecUtils_EnergyCal_valid( const SpecUtils_EnergyCal * const instance );
    
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_EnergyCal_number_coefficients( const SpecUtils_EnergyCal * const instance );
    
DLLEXPORT const float * CALLINGCONVENTION
SpecUtils_EnergyCal_coefficients( const SpecUtils_EnergyCal * const instance );

DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_EnergyCal_number_deviation_pairs( const SpecUtils_EnergyCal * const instance );
    
DLLEXPORT float CALLINGCONVENTION
SpecUtils_EnergyCal_deviation_energy( const SpecUtils_EnergyCal * const instance,
                                       const uint32_t deviation_pair_index );
DLLEXPORT float CALLINGCONVENTION
SpecUtils_EnergyCal_deviation_offset( const SpecUtils_EnergyCal * const instance,
                                         const uint32_t deviation_pair_index );
    
DLLEXPORT uint32_t CALLINGCONVENTION
SpecUtils_EnergyCal_number_channels( const SpecUtils_EnergyCal * const instance );

/** The channel lower energies array.
   Will have one more entry that the number of channels (the last entry gives last channel upper energy).
*/
DLLEXPORT const float * CALLINGCONVENTION
SpecUtils_EnergyCal_channel_energies( const SpecUtils_EnergyCal * const instance );
    
/** Sets the polynomial coefficients, and non-linear deviation pairs for the energy calibration object.
   
  @param instance The `SpecUtils_EnergyCal` to modify.
  @param num_channels The number of channels this energy calibration is for.
  @param coeffs The array of polynomial energy calibration coefficients.
  @param number_coeffs The number of entries in the `coeffs` array.  Must be at least two coefficients.
  @param dev_pairs An array giving deviation pairs where the entries are energy followed by offset, e.g.,
          for 3 deviations pairs, the entries in this array would be: [energy_0, offset_0, energy_1, offset_1, energy_2, offset_2]
          May be `NULL`.
  @param number_dev_pairs The number of deviation pairs in the `dev_pairs` array; that is the
          `dev_pairs` array must have twice this many entries in it.
  @returns If the energy calibration supplied is valid, and hence the `SpecUtils_EnergyCal` instance updated.
            Will return false if coefficients or deviation pairs are invalid (e.g., not enough coefficients, NaN of Inf coefficients,
            results in non monotonically increasing channel energies, or are otherwise unreasonable).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_EnergyCal_set_polynomial( SpecUtils_EnergyCal * instance,
                                    const uint32_t num_channels,
                                    const float *coeffs,
                                    const uint32_t number_coeffs,
                                    const float * const dev_pairs,
                                    const uint32_t number_dev_pairs );

/** Sets the full-range fraction coefficients, and non-linear deviation pairs for the energy calibration object.
     
  @param instance The `SpecUtils_EnergyCal` to modify.
  @param num_channels The number of channels this energy calibration is for.
  @param coeffs The array of polynomial energy calibration coefficients.
  @param number_coeffs The number of entries in the `coeffs` array.  Must be at least two coefficients, and
          should be 5 or fewer coefficients.
  @param dev_pairs An array giving deviation pairs where the entries are energy followed by offset, e.g.,
          for 3 deviations pairs, the entries in this array would be: [energy_0, offset_0, energy_1, offset_1, energy_2, offset_2]
          May be `NULL`.
  @param number_dev_pairs The number of deviation pairs in the `dev_pairs` array; that is the
          `dev_pairs` array must have twice this many entries in it.
  @returns If the energy calibration supplied is valid, and hence the `SpecUtils_EnergyCal` instance updated.
            Will return false if coefficients or deviation pairs are invalid (e.g., not enough coefficients, NaN of Inf coefficients,
            results in non monotonically increasing channel energies, or are otherwise unreasonable).
*/
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_EnergyCal_set_full_range_fraction( SpecUtils_EnergyCal * instance,
                                              const uint32_t num_channels,
                                              const float *coeffs,
                                              const uint32_t num_coeffs,
                                              const float * const dev_pairs,
                                              const uint32_t number_dev_pairs );
  /** Sets the lower-channel energies for the energy calibration object.
   
   @param instance The `SpecUtils_EnergyCal` to modify.
   @param num_channels The number of channels this energy calibration is for.
   @param num_energies The number of entries in the energy channel array.  This must be at least
          as large as `num_channels`, but will typically be 1 larger than this to specify the last channels
          upper energy.  If larger than `num_channels + 1`, all subsequent channels will be ignored.
   @param channel_energies The lower energies of each channel.
   
   @returns If the energy calibration supplied is valid, and hence the `SpecUtils_EnergyCal` instance updated.
            Will return false if input energies are invalid, not increasing, or not enough of them for the number of channels.
   */
DLLEXPORT bool CALLINGCONVENTION
SpecUtils_EnergyCal_set_lower_channel_energy( SpecUtils_EnergyCal * instance,
                                              const uint32_t num_channels,
                                              const uint32_t num_energies,
                                              const float * const channel_energies );

/** Returns the fractional channel number a given energy corresponds to.
   
  If energy calibration coefficients are not set, then will return -999.9.
   
  \sa SpecUtils_EnergyCal_valid
*/
DLLEXPORT double CALLINGCONVENTION
SpecUtils_EnergyCal_channel_for_energy( const SpecUtils_EnergyCal * const instance,
                                          const double energy );

/** Returns the energy for a fractional channel.  That is, if channel is an integer, will return the lower energy of channel, and if
   the fractional part of the channel is 0.5, will return the midpoint of the channel.
   
   If energy calibration coefficients are not set, then will return -999.9.
*/
DLLEXPORT double CALLINGCONVENTION
SpecUtils_EnergyCal_energy_for_channel( const SpecUtils_EnergyCal * const instance,
                                           const double channel );

/** Returns the lower energy this energy calibration is valid for.
   
   If energy calibration coefficients are not set, then will return -999.9f.
   
  \sa SpecUtils_EnergyCal_valid
*/
DLLEXPORT float CALLINGCONVENTION
SpecUtils_EnergyCal_lower_energy( const SpecUtils_EnergyCal * const instance );

/** Returns the upper energy (e.g., upper energy of the last channel) this energy calibration is valid for.
     
     If energy calibration coefficients are not set, then will return -999.9f.
     
     \sa SpecUtils_EnergyCal_valid
*/
DLLEXPORT float CALLINGCONVENTION
SpecUtils_EnergyCal_upper_energy( const SpecUtils_EnergyCal * const instance );
    
    
    
    /*
    //Free standing energy calibration functions that could be useful to expose to C
     
    std::shared_ptr<EnergyCalibration> energy_cal_combine_channels( const EnergyCalibration &orig_cal,
                                                                   const size_t num_channel_combine );
    double fullrangefraction_energy( const double channel_number,
                                   const std::vector<float> &coeffs,
                                   const size_t nchannel,
                                   const std::vector<std::pair<float,float>> &deviation_pairs );
    double polynomial_energy( const double channel_number,
                             const std::vector<float> &coeffs,
                             const std::vector<std::pair<float,float>> &deviation_pairs );
    std::vector<float>
    polynomial_coef_to_fullrangefraction( const std::vector<float> &coeffs,
                                          const size_t nchannel );
    std::vector<float>
    fullrangefraction_coef_to_polynomial( const std::vector<float> &coeffs,
                                          const size_t nchannel );
    bool calibration_is_valid( const EnergyCalType type,
                                     const std::vector<float> &eqn,
                                     const std::vector< std::pair<float,float> > &devpairs,
                                     size_t nbin );
  */
  
#ifdef __cplusplus
}
#endif

#endif // SpecUtils_c_h
