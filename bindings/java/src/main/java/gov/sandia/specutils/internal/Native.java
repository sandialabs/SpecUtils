package gov.sandia.specutils.internal;

/**
 * Package-internal JNI bridge. One native method per function in
 * {@code bindings/c/SpecUtils_c.h}. Users should not call these directly —
 * use {@link gov.sandia.specutils.SpecFile},
 * {@link gov.sandia.specutils.Measurement}, and
 * {@link gov.sandia.specutils.EnergyCalibration} instead.
 *
 * All handle parameters are Java {@code long} values holding the value of
 * the corresponding C pointer. A handle of {@code 0L} indicates a
 * non-existent object; the JNI glue throws
 * {@link IllegalStateException} when it encounters one.
 *
 * Enums cross the boundary as their {@code ordinal()} value (a plain int);
 * the high-level wrapper classes convert them to/from the public Java
 * {@code enum} types.
 *
 * Strings cross as {@code java.lang.String} and are converted to UTF-8
 * inside the glue (the C API requires UTF-8, even on Windows).
 *
 * Primitive arrays ({@code float[]}, {@code int[]}, {@code String[]})
 * cross by copy — the glue uses {@code GetXxxArrayElements} /
 * {@code SetXxxArrayRegion} to move data, and arrays returned from native
 * are freshly-allocated Java arrays.
 */
public final class Native {

    private Native() { }

    static {
        NativeLibraryLoader.ensureLoaded();
    }

    /**
     * Called by wrapper classes in their static initializer to force the
     * native library to load before any {@code native} method is invoked.
     */
    public static void init() {
        // Triggers static initializer via classload.
    }

    // ===================================================================
    //  SpecFile lifecycle
    // ===================================================================

    public static native long specFileCreate();
    public static native long specFileClone(long handle);
    public static native void specFileDestroy(long handle);
    public static native void specFileSetEqual(long lhsHandle, long rhsHandle);
    public static native void specFileReset(long handle);

    // ===================================================================
    //  SpecFile parsing & writing
    // ===================================================================

    /** @return {@code true} on success, {@code false} on failure. */
    public static native boolean specFileLoadFile(long handle, String filename);

    /** @param parserTypeOrdinal ordinal of {@code gov.sandia.specutils.ParserType}. */
    public static native boolean specFileLoadFileFromFormat(long handle, String filename,
                                                            int parserTypeOrdinal);

    /** @param saveTypeOrdinal ordinal of {@code gov.sandia.specutils.SaveSpectrumAsType}. */
    public static native boolean specFileWriteToFile(long handle, String filename,
                                                     int saveTypeOrdinal);

    // ===================================================================
    //  SpecFile queries
    // ===================================================================

    public static native boolean specFilePassthrough(long handle);
    public static native int specFileNumberMeasurements(long handle);
    public static native int specFileNumberGammaChannels(long handle);
    public static native boolean specFileModified(long handle);
    public static native int specFileMemorySize(long handle);

    /** @return handle to measurement owned by the SpecFile, or 0 if index invalid. */
    public static native long specFileGetMeasurementByIndex(long handle, int index);

    /** @return handle to measurement owned by the SpecFile, or 0 if not found. */
    public static native long specFileGetMeasurementBySampleDet(long handle, int sampleNumber,
                                                                String detName);

    /** @return a reference-counted measurement handle (caller frees via countedRefMeasurementDestroy),
     *  or 0 if index invalid.  Keeps the measurement alive independent of the SpecFile. */
    public static native long specFileGetMeasurementRefByIndex(long handle, int index);

    /** @return a reference-counted measurement handle (caller frees via countedRefMeasurementDestroy),
     *  or 0 if not found.  Keeps the measurement alive independent of the SpecFile. */
    public static native long specFileGetMeasurementRefBySampleDet(long handle, int sampleNumber,
                                                                   String detName);

    /** @return the (non-owning) measurement view handle held by a reference-counted handle, or 0. */
    public static native long measurementPtrFromRef(long refHandle);

    /** Releases a reference-counted measurement handle from specFileGetMeasurementRef*. */
    public static native void countedRefMeasurementDestroy(long refHandle);

    public static native int specFileNumberDetectors(long handle);
    public static native String specFileDetectorName(long handle, int index);
    public static native int specFileNumberGammaDetectors(long handle);
    public static native String specFileGammaDetectorName(long handle, int index);
    public static native int specFileNumberNeutronDetectors(long handle);
    public static native String specFileNeutronDetectorName(long handle, int index);
    public static native int specFileNumberSamples(long handle);
    public static native int specFileSampleNumber(long handle, int index);

    public static native int specFileNumberRemarks(long handle);
    public static native String specFileRemark(long handle, int remarkIndex);
    public static native int specFileNumberParseWarnings(long handle);
    public static native String specFileParseWarning(long handle, int warningIndex);

    public static native float specFileSumGammaLiveTime(long handle);
    public static native float specFileSumGammaRealTime(long handle);
    public static native double specFileGammaCountSum(long handle);
    public static native double specFileNeutronCountsSum(long handle);

    public static native String specFileFilename(long handle);
    public static native String specFileUuid(long handle);
    public static native String specFileMeasurementLocationName(long handle);
    public static native String specFileMeasurementOperator(long handle);

    /** @return ordinal of {@code gov.sandia.specutils.DetectorType}. */
    public static native int specFileDetectorType(long handle);
    public static native String specFileInstrumentType(long handle);
    public static native String specFileManufacturer(long handle);
    public static native String specFileInstrumentModel(long handle);
    public static native String specFileInstrumentId(long handle);

    public static native boolean specFileHasGpsInfo(long handle);
    public static native double specFileMeanLatitude(long handle);
    public static native double specFileMeanLongitude(long handle);
    public static native boolean specFileContainsDerivedData(long handle);
    public static native boolean specFileContainsNonDerivedData(long handle);

    // ===================================================================
    //  SpecFile mutators
    // ===================================================================

    /** @return new Measurement handle owned by caller, or 0 on failure. */
    public static native long specFileSumMeasurements(long handle, int[] sampleNumbers,
                                                      String[] detectorNames);

    /**
     * Transfers ownership of {@code measurementHandle} to the SpecFile on
     * success. On failure the caller keeps ownership.
     * @return {@code true} if the measurement was accepted.
     */
    public static native boolean specFileAddMeasurement(long handle, long measurementHandle,
                                                        boolean doCleanup);

    /**
     * Transfers ownership of the removed measurement back to the caller on
     * success (though the C layer may destroy it internally — see
     * SpecUtils_c.h).
     */
    public static native boolean specFileRemoveMeasurement(long handle, long measurementHandle,
                                                           boolean doCleanup);

    public static native boolean specFileRemoveMeasurements(long handle, long[] measurementHandles);

    public static native void specFileCleanup(long handle, boolean dontChangeSampleNumbers,
                                              boolean reorderByTime);

    public static native void specFileSetFilename(long handle, String filename);
    public static native void specFileSetRemarks(long handle, String[] remarks);
    public static native void specFileAddRemark(long handle, String remark);
    public static native void specFileSetParseWarnings(long handle, String[] warnings);
    public static native void specFileSetUuid(long handle, String uuid);
    public static native void specFileSetLaneNumber(long handle, int laneNumber);
    public static native void specFileSetMeasurementLocationName(long handle, String locationName);
    public static native void specFileSetInspection(long handle, String inspectionType);
    public static native void specFileSetInstrumentType(long handle, String instrumentType);
    public static native void specFileSetDetectorType(long handle, int detectorTypeOrdinal);
    public static native void specFileSetManufacturer(long handle, String manufacturer);
    public static native void specFileSetInstrumentModel(long handle, String model);
    public static native void specFileSetInstrumentId(long handle, String serialNumber);

    public static native boolean specFileChangeDetectorName(long handle, String oldName,
                                                            String newName);
    public static native boolean specFileSetEnergyCalibrationFromCALpFile(long handle,
                                                                         String calpPath);

    public static native boolean specFileSetMeasurementLiveTime(long handle, float liveTime,
                                                                long measurementHandle);
    public static native boolean specFileSetMeasurementRealTime(long handle, float realTime,
                                                                long measurementHandle);
    public static native boolean specFileSetMeasurementStartTimeUsecs(long handle,
                                                                     long microsecondsSinceEpoch,
                                                                     long measurementHandle);
    public static native boolean specFileSetMeasurementStartTimeStr(long handle, String dateTime,
                                                                    long measurementHandle);
    public static native boolean specFileSetMeasurementRemarks(long handle, String[] remarks,
                                                               long measurementHandle);
    public static native boolean specFileSetMeasurementSourceType(long handle,
                                                                  int sourceTypeOrdinal,
                                                                  long measurementHandle);
    public static native boolean specFileSetMeasurementPosition(long handle, double longitude,
                                                                double latitude,
                                                                long microsecondsSinceEpoch,
                                                                long measurementHandle);
    public static native boolean specFileSetMeasurementTitle(long handle, String title,
                                                             long measurementHandle);
    public static native boolean specFileSetMeasurementContainedNeutrons(long handle,
                                                                         boolean contained,
                                                                         float counts,
                                                                         float neutronLiveTime,
                                                                         long measurementHandle);
    public static native boolean specFileSetMeasurementEnergyCalibration(long handle,
                                                                         long countedRefCalHandle,
                                                                         long measurementHandle);

    // ===================================================================
    //  Measurement lifecycle
    // ===================================================================

    public static native long measurementCreate();
    public static native long measurementClone(long handle);
    public static native void measurementDestroy(long handle);
    public static native int measurementMemorySize(long handle);
    public static native void measurementSetEqual(long lhsHandle, long rhsHandle);
    public static native void measurementReset(long handle);

    // ===================================================================
    //  Measurement metadata accessors
    // ===================================================================

    public static native String measurementDescription(long handle);
    public static native void measurementSetDescription(long handle, String description);
    public static native String measurementSourceString(long handle);
    public static native void measurementSetSourceString(long handle, String source);
    public static native String measurementTitle(long handle);
    public static native void measurementSetTitle(long handle, String title);

    /** @return microseconds since 1970-01-01 UTC, or 0 if unset. */
    public static native long measurementStartTimeUsecs(long handle);
    public static native void measurementSetStartTimeUsecs(long handle,
                                                           long microsecondsSinceEpoch);
    public static native boolean measurementSetStartTimeStr(long handle, String startTime);

    public static native char measurementPcfTag(long handle);
    public static native void measurementSetPcfTag(long handle, char tag);

    public static native int measurementNumberGammaChannels(long handle);

    /**
     * @return newly-allocated float[] with {@link #measurementNumberGammaChannels}
     *         entries, or an empty array if no counts are set.
     */
    public static native float[] measurementGammaChannelCounts(long handle);

    /**
     * @return newly-allocated float[] with
     *         {@link #measurementNumberGammaChannels} + 1 entries giving
     *         channel lower edges, or an empty array if unset.
     */
    public static native float[] measurementEnergyBounds(long handle);

    /**
     * Non-owning pointer to the Measurement's energy calibration.
     * @return handle, or 0 if unset. Caller must NOT destroy.
     */
    public static native long measurementEnergyCalibrationPtr(long handle);

    /**
     * Counted reference to the Measurement's energy calibration. Caller
     * owns the returned handle and must call
     * {@link #countedRefEnergyCalDestroy(long)}.
     * @return handle, or 0 if unset.
     */
    public static native long measurementEnergyCalibrationRef(long handle);

    public static native void measurementSetGammaCounts(long handle, float[] counts,
                                                        float liveTime, float realTime);
    public static native void measurementSetNeutronCounts(long handle, float[] counts,
                                                          float neutronLiveTime);

    public static native float measurementRealTime(long handle);
    public static native float measurementLiveTime(long handle);
    public static native float measurementNeutronLiveTime(long handle);
    public static native double measurementGammaCountSum(long handle);
    public static native double measurementNeutronCountSum(long handle);
    public static native boolean measurementIsOccupied(long handle);
    public static native boolean measurementContainedNeutron(long handle);

    public static native int measurementSampleNumber(long handle);
    public static native void measurementSetSampleNumber(long handle, int sampleNumber);
    public static native String measurementDetectorName(long handle);
    public static native void measurementSetDetectorName(long handle, String name);
    public static native float measurementSpeed(long handle);

    /** @return ordinal of {@code gov.sandia.specutils.OccupancyStatus}. */
    public static native int measurementOccupancyStatus(long handle);
    public static native void measurementSetOccupancyStatus(long handle, int ordinal);

    public static native boolean measurementHasGpsInfo(long handle);
    public static native double measurementLatitude(long handle);
    public static native double measurementLongitude(long handle);
    public static native long measurementPositionTimeMicrosec(long handle);
    public static native void measurementSetPosition(long handle, double longitude,
                                                     double latitude,
                                                     long microsecondsSinceEpoch);

    public static native float measurementDoseRate(long handle);
    public static native float measurementExposureRate(long handle);
    public static native String measurementDetectorType(long handle);

    /** @return ordinal of {@code gov.sandia.specutils.QualityStatus}. */
    public static native int measurementQualityStatus(long handle);
    /** @return ordinal of {@code gov.sandia.specutils.SourceType}. */
    public static native int measurementSourceType(long handle);
    public static native void measurementSetSourceType(long handle, int ordinal);

    public static native int measurementNumberRemarks(long handle);
    public static native String measurementRemark(long handle, int remarkIndex);
    public static native void measurementSetRemarks(long handle, String[] remarks);
    public static native int measurementNumberParseWarnings(long handle);
    public static native String measurementParseWarning(long handle, int warningIndex);

    public static native double measurementGammaIntegral(long handle, float lowerEnergy,
                                                         float upperEnergy);
    public static native double measurementGammaChannelsSum(long handle, int startBin, int endBin);
    public static native int measurementDerivedDataProperties(long handle);

    public static native boolean measurementCombineGammaChannels(long handle, int nChannel);
    public static native boolean measurementRebin(long handle, long countedRefCalHandle);
    public static native boolean measurementSetEnergyCalibration(long handle,
                                                                 long countedRefCalHandle);

    // ===================================================================
    //  EnergyCalibration
    // ===================================================================

    public static native long energyCalCreate();
    public static native void energyCalDestroy(long handle);
    public static native long countedRefEnergyCalCreate();
    public static native void countedRefEnergyCalDestroy(long handle);

    /**
     * Returns a non-owning EnergyCal pointer from a counted ref.
     * Caller must NOT destroy the returned handle.
     */
    public static native long energyCalPtrFromRef(long countedRefHandle);

    /**
     * Converts a caller-owned EnergyCal into a counted ref. The original
     * handle must not be destroyed after this call.
     */
    public static native long energyCalMakeCountedRef(long energyCalHandle);

    /** @return ordinal of {@code gov.sandia.specutils.EnergyCalType}. */
    public static native int energyCalType(long handle);
    public static native boolean energyCalValid(long handle);
    public static native int energyCalNumberCoefficients(long handle);
    public static native float[] energyCalCoefficients(long handle);
    public static native int energyCalNumberDeviationPairs(long handle);
    public static native float energyCalDeviationEnergy(long handle, int pairIndex);
    public static native float energyCalDeviationOffset(long handle, int pairIndex);
    public static native int energyCalNumberChannels(long handle);
    public static native float[] energyCalChannelEnergies(long handle);

    public static native boolean energyCalSetPolynomial(long handle, int numChannels,
                                                        float[] coeffs, float[] devPairs);
    public static native boolean energyCalSetFullRangeFraction(long handle, int numChannels,
                                                               float[] coeffs, float[] devPairs);
    public static native boolean energyCalSetLowerChannelEnergy(long handle, int numChannels,
                                                                float[] channelEnergies);
    public static native double energyCalChannelForEnergy(long handle, double energy);
    public static native double energyCalEnergyForChannel(long handle, double channel);
    public static native float energyCalLowerEnergy(long handle);
    public static native float energyCalUpperEnergy(long handle);
}
