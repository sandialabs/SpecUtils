package gov.sandia.specutils;

import gov.sandia.specutils.internal.HandleCleaner;
import gov.sandia.specutils.internal.HandleCleaner.Registration;
import gov.sandia.specutils.internal.Native;

/**
 * A parsed (or under-construction) gamma spectrum file containing one or
 * more {@link Measurement} records plus file-level metadata.
 *
 * <p>Always obtain Measurement objects via this class's methods while the
 * SpecFile is still open — a Measurement returned by
 * {@link #measurement(int)} shares lifetime with the parent and will be
 * invalidated when the parent is closed.
 *
 * <h3>Example</h3>
 * <pre>{@code
 * try (SpecFile f = new SpecFile()) {
 *     f.loadFile("detector.n42");
 *     for (int i = 0; i < f.numberMeasurements(); i++) {
 *         Measurement m = f.measurement(i);  // borrowed, don't close
 *         System.out.println(m.liveTime());
 *     }
 *     f.writeToFile("detector.pcf", SaveSpectrumAsType.Pcf);
 * }
 * }</pre>
 */
public final class SpecFile implements AutoCloseable {

    static { Native.init(); }

    long handle;
    private final Registration cleanerRegistration;
    private final Object cleanerOwner = new Object();

    public SpecFile() {
        this.handle = Native.specFileCreate();
        if (this.handle == 0L) {
            throw new SpecUtilsException("specFileCreate returned null");
        }
        final long h = this.handle;
        this.cleanerRegistration = HandleCleaner.register(cleanerOwner, () -> {
            if (h != 0L) Native.specFileDestroy(h);
        });
    }

    /** Creates a deep copy of this SpecFile. */
    public SpecFile cloneFile() {
        throwIfClosed();
        long h = Native.specFileClone(handle);
        if (h == 0L) throw new SpecUtilsException("specFileClone returned null");
        SpecFile copy = new SpecFile();
        // Replace the fresh handle in copy with the cloned one
        Native.specFileDestroy(copy.handle);
        copy.handle = h;
        return copy;
    }

    public void setEqual(SpecFile other) {
        throwIfClosed();
        if (other == null) throw new IllegalArgumentException("other");
        Native.specFileSetEqual(handle, other.handle);
    }

    public void reset() {
        throwIfClosed();
        Native.specFileReset(handle);
    }

    // ===== File I/O =====

    public void loadFile(String filename) {
        throwIfClosed();
        if (!Native.specFileLoadFile(handle, filename)) {
            throw new SpecUtilsException("loadFile failed for '" + filename + "'");
        }
    }

    public void loadFile(String filename, ParserType type) {
        throwIfClosed();
        if (!Native.specFileLoadFileFromFormat(handle, filename, type.ordinal())) {
            throw new SpecUtilsException("loadFile(" + type + ") failed for '" + filename + "'");
        }
    }

    public void writeToFile(String filename, SaveSpectrumAsType type) {
        throwIfClosed();
        if (!Native.specFileWriteToFile(handle, filename, type.ordinal())) {
            throw new SpecUtilsException("writeToFile(" + type + ") failed for '"
                + filename + "'");
        }
    }

    // ===== File-level queries =====

    public boolean passthrough()       { throwIfClosed(); return Native.specFilePassthrough(handle); }
    public int numberMeasurements()    { throwIfClosed(); return Native.specFileNumberMeasurements(handle); }
    public int numberGammaChannels()   { throwIfClosed(); return Native.specFileNumberGammaChannels(handle); }
    public boolean modified()          { throwIfClosed(); return Native.specFileModified(handle); }
    public int memorySize()            { throwIfClosed(); return Native.specFileMemorySize(handle); }

    public int numberDetectors()        { throwIfClosed(); return Native.specFileNumberDetectors(handle); }
    public String detectorName(int i)   { throwIfClosed(); return Native.specFileDetectorName(handle, i); }
    public int numberGammaDetectors()   { throwIfClosed(); return Native.specFileNumberGammaDetectors(handle); }
    public String gammaDetectorName(int i) { throwIfClosed(); return Native.specFileGammaDetectorName(handle, i); }
    public int numberNeutronDetectors() { throwIfClosed(); return Native.specFileNumberNeutronDetectors(handle); }
    public String neutronDetectorName(int i) { throwIfClosed(); return Native.specFileNeutronDetectorName(handle, i); }

    public int numberSamples()       { throwIfClosed(); return Native.specFileNumberSamples(handle); }
    public int sampleNumber(int i)   { throwIfClosed(); return Native.specFileSampleNumber(handle, i); }

    public int numberRemarks()       { throwIfClosed(); return Native.specFileNumberRemarks(handle); }
    public String remark(int i)      { throwIfClosed(); return Native.specFileRemark(handle, i); }
    public int numberParseWarnings() { throwIfClosed(); return Native.specFileNumberParseWarnings(handle); }
    public String parseWarning(int i){ throwIfClosed(); return Native.specFileParseWarning(handle, i); }

    public float sumGammaLiveTime()  { throwIfClosed(); return Native.specFileSumGammaLiveTime(handle); }
    public float sumGammaRealTime()  { throwIfClosed(); return Native.specFileSumGammaRealTime(handle); }
    public double gammaCountSum()    { throwIfClosed(); return Native.specFileGammaCountSum(handle); }
    public double neutronCountsSum() { throwIfClosed(); return Native.specFileNeutronCountsSum(handle); }

    public String filename()                  { throwIfClosed(); return Native.specFileFilename(handle); }
    public String uuid()                      { throwIfClosed(); return Native.specFileUuid(handle); }
    public String measurementLocationName()   { throwIfClosed(); return Native.specFileMeasurementLocationName(handle); }
    public String measurementOperator()       { throwIfClosed(); return Native.specFileMeasurementOperator(handle); }
    public String instrumentType()            { throwIfClosed(); return Native.specFileInstrumentType(handle); }
    public String manufacturer()              { throwIfClosed(); return Native.specFileManufacturer(handle); }
    public String instrumentModel()           { throwIfClosed(); return Native.specFileInstrumentModel(handle); }
    public String instrumentId()              { throwIfClosed(); return Native.specFileInstrumentId(handle); }

    public DetectorType detectorType() {
        throwIfClosed();
        return DetectorType.fromOrdinal(Native.specFileDetectorType(handle));
    }

    public boolean hasGpsInfo()       { throwIfClosed(); return Native.specFileHasGpsInfo(handle); }
    public double meanLatitude()      { throwIfClosed(); return Native.specFileMeanLatitude(handle); }
    public double meanLongitude()     { throwIfClosed(); return Native.specFileMeanLongitude(handle); }
    public boolean containsDerivedData()    { throwIfClosed(); return Native.specFileContainsDerivedData(handle); }
    public boolean containsNonDerivedData() { throwIfClosed(); return Native.specFileContainsNonDerivedData(handle); }

    // ===== Measurement access =====

    /**
     * Returns a borrowed wrapper around the measurement at the given index.
     * The returned Measurement's lifetime follows this SpecFile —
     * {@link Measurement#close()} is a no-op.
     * @return null if {@code index} is out of range.
     */
    public Measurement measurement(int index) {
        throwIfClosed();
        long mh = Native.specFileGetMeasurementByIndex(handle, index);
        if (mh == 0L) return null;
        return new Measurement(mh, false, this);
    }

    public Measurement measurement(int sampleNumber, String detectorName) {
        throwIfClosed();
        long mh = Native.specFileGetMeasurementBySampleDet(handle, sampleNumber, detectorName);
        if (mh == 0L) return null;
        return new Measurement(mh, false, this);
    }

    /**
     * Creates a new owned Measurement that is the sum of the measurements
     * matching the given sample numbers and detector names. Returns null
     * if no measurements match.
     */
    public Measurement sumMeasurements(int[] sampleNumbers, String[] detectorNames) {
        throwIfClosed();
        long mh = Native.specFileSumMeasurements(handle, sampleNumbers, detectorNames);
        if (mh == 0L) return null;
        return new Measurement(mh, true, null);
    }

    /**
     * Transfers ownership of {@code m} to this SpecFile. On success,
     * {@code m.close()} becomes a no-op (the SpecFile owns it now).
     */
    public void addMeasurement(Measurement m, boolean doCleanup) {
        throwIfClosed();
        if (m == null) throw new IllegalArgumentException("measurement");
        if (m.handle == 0L) throw new IllegalStateException("measurement closed");
        if (!Native.specFileAddMeasurement(handle, m.handle, doCleanup)) {
            throw new SpecUtilsException("addMeasurement failed");
        }
        m.transferOwnership();
    }

    public boolean removeMeasurement(Measurement m, boolean doCleanup) {
        throwIfClosed();
        if (m == null || m.handle == 0L) return false;
        return Native.specFileRemoveMeasurement(handle, m.handle, doCleanup);
    }

    public void cleanup(boolean dontChangeSampleNumbers, boolean reorderByTime) {
        throwIfClosed();
        Native.specFileCleanup(handle, dontChangeSampleNumbers, reorderByTime);
    }

    // ===== File-level mutators =====

    public void setFilename(String s)                { throwIfClosed(); Native.specFileSetFilename(handle, s); }
    public void setRemarks(String[] remarks)         { throwIfClosed(); Native.specFileSetRemarks(handle, remarks); }
    public void addRemark(String s)                  { throwIfClosed(); Native.specFileAddRemark(handle, s); }
    public void setParseWarnings(String[] warnings)  { throwIfClosed(); Native.specFileSetParseWarnings(handle, warnings); }
    public void setUuid(String s)                    { throwIfClosed(); Native.specFileSetUuid(handle, s); }
    public void setLaneNumber(int n)                 { throwIfClosed(); Native.specFileSetLaneNumber(handle, n); }
    public void setMeasurementLocationName(String s) { throwIfClosed(); Native.specFileSetMeasurementLocationName(handle, s); }
    public void setInspection(String s)              { throwIfClosed(); Native.specFileSetInspection(handle, s); }
    public void setInstrumentType(String s)          { throwIfClosed(); Native.specFileSetInstrumentType(handle, s); }
    public void setDetectorType(DetectorType t)      { throwIfClosed(); Native.specFileSetDetectorType(handle, t.ordinal()); }
    public void setManufacturer(String s)            { throwIfClosed(); Native.specFileSetManufacturer(handle, s); }
    public void setInstrumentModel(String s)         { throwIfClosed(); Native.specFileSetInstrumentModel(handle, s); }
    public void setInstrumentId(String s)            { throwIfClosed(); Native.specFileSetInstrumentId(handle, s); }

    public boolean changeDetectorName(String oldName, String newName) {
        throwIfClosed();
        return Native.specFileChangeDetectorName(handle, oldName, newName);
    }

    public boolean setEnergyCalibrationFromCALpFile(String calpPath) {
        throwIfClosed();
        return Native.specFileSetEnergyCalibrationFromCALpFile(handle, calpPath);
    }

    // ===== Per-measurement mutators (only valid for measurements owned by THIS SpecFile) =====

    public void setMeasurementLiveTime(float liveTime, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementLiveTime(handle, liveTime, m.handle)) {
            throw new SpecUtilsException("setMeasurementLiveTime: measurement not owned by this SpecFile");
        }
    }

    public void setMeasurementRealTime(float realTime, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementRealTime(handle, realTime, m.handle)) {
            throw new SpecUtilsException("setMeasurementRealTime: measurement not owned by this SpecFile");
        }
    }

    public void setMeasurementStartTime(long microsecondsSinceEpoch, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementStartTimeUsecs(handle, microsecondsSinceEpoch, m.handle)) {
            throw new SpecUtilsException("setMeasurementStartTime: not owned");
        }
    }

    public void setMeasurementStartTime(String dateTime, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementStartTimeStr(handle, dateTime, m.handle)) {
            throw new SpecUtilsException("setMeasurementStartTime(str): parse failure or not owned");
        }
    }

    public void setMeasurementRemarks(String[] remarks, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementRemarks(handle, remarks, m.handle)) {
            throw new SpecUtilsException("setMeasurementRemarks: not owned");
        }
    }

    public void setMeasurementSourceType(SourceType type, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementSourceType(handle, type.ordinal(), m.handle)) {
            throw new SpecUtilsException("setMeasurementSourceType: not owned");
        }
    }

    public void setMeasurementPosition(double longitude, double latitude,
                                       long microsecondsSinceEpoch, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementPosition(handle, longitude, latitude,
                microsecondsSinceEpoch, m.handle)) {
            throw new SpecUtilsException("setMeasurementPosition: not owned");
        }
    }

    public void setMeasurementTitle(String title, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementTitle(handle, title, m.handle)) {
            throw new SpecUtilsException("setMeasurementTitle: not owned");
        }
    }

    public void setMeasurementContainedNeutrons(boolean contained, float counts,
                                                float neutronLiveTime, Measurement m) {
        throwIfClosed();
        if (!Native.specFileSetMeasurementContainedNeutrons(handle, contained, counts,
                neutronLiveTime, m.handle)) {
            throw new SpecUtilsException("setMeasurementContainedNeutrons: not owned");
        }
    }

    public void setMeasurementEnergyCalibration(EnergyCalibration cal, Measurement m) {
        throwIfClosed();
        if (cal == null || cal.countedRefHandle == 0L) {
            throw new IllegalArgumentException(
                "setMeasurementEnergyCalibration requires a counted-ref EnergyCalibration"
                + " (call toCountedRef())");
        }
        if (!Native.specFileSetMeasurementEnergyCalibration(handle, cal.countedRefHandle, m.handle)) {
            throw new SpecUtilsException("setMeasurementEnergyCalibration failed");
        }
    }

    @Override
    public synchronized void close() {
        if (handle == 0L) return;
        cleanerRegistration.deregister();
        Native.specFileDestroy(handle);
        handle = 0L;
    }

    private void throwIfClosed() {
        if (handle == 0L) {
            throw new IllegalStateException("SpecFile has been closed");
        }
    }
}
