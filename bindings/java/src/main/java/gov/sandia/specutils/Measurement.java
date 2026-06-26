package gov.sandia.specutils;

import gov.sandia.specutils.internal.HandleCleaner;
import gov.sandia.specutils.internal.HandleCleaner.Registration;
import gov.sandia.specutils.internal.Native;

/**
 * A single spectrum record (one detector, one sample interval) within a
 * {@link SpecFile}.
 *
 * <p>Instances may be <i>owned</i> (created via {@link #Measurement()} or
 * via {@link SpecFile#sumMeasurements}) or obtained from a SpecFile via
 * {@link SpecFile#measurement(int)}. The latter are <i>reference-counted</i>:
 * they keep the underlying measurement alive independent of the SpecFile, so
 * they remain valid even after the SpecFile is modified or closed. Either kind
 * should be {@link #close()}d when done. Transferring ownership to a SpecFile
 * (via {@link SpecFile#addMeasurement}) flips an owned measurement to
 * non-owning on success.
 */
public final class Measurement implements AutoCloseable {

    static { Native.init(); }

    long handle;
    private boolean ownsHandle;
    // For measurements obtained from a SpecFile: an owning reference-counted handle that keeps the
    // underlying measurement alive (0 for owned measurements).  `handle` is then a view into it.
    private long refHandle;
    // Held to keep the parent SpecFile from being GC'd while in use (not required for lifetime
    // safety any more - the ref handle owns the measurement - but harmless and convenient).
    @SuppressWarnings("unused")
    private final SpecFile parent;
    private Registration cleanerRegistration;
    private final Object cleanerOwner = new Object();

    /** Creates a new owned, empty Measurement. */
    public Measurement() {
        this.handle = Native.measurementCreate();
        if (this.handle == 0L) {
            throw new SpecUtilsException("measurementCreate returned null");
        }
        this.ownsHandle = true;
        this.parent = null;
        this.cleanerRegistration = registerCleaner();
    }

    /** Internal: wraps an existing owned handle (e.g. from clone or sum). */
    Measurement(long handle, boolean ownsHandle, SpecFile parent) {
        this.handle = handle;
        this.ownsHandle = ownsHandle;
        this.refHandle = 0L;
        this.parent = parent;
        this.cleanerRegistration = ownsHandle ? registerCleaner() : null;
    }

    /** Internal: wraps a reference-counted handle obtained from a SpecFile. */
    private Measurement(long viewHandle, long refHandle, SpecFile parent) {
        this.handle = viewHandle;
        this.ownsHandle = false;
        this.refHandle = refHandle;
        this.parent = parent;
        this.cleanerRegistration = registerCleaner();
    }

    /**
     * Wraps a reference-counted measurement handle (from Native.specFileGetMeasurementRef*).
     * The ref keeps the underlying measurement alive, so this wrapper stays valid even after the
     * parent SpecFile is modified or closed.
     */
    static Measurement fromRef(long refHandle, SpecFile parent) {
        final long view = Native.measurementPtrFromRef(refHandle);
        if (view == 0L) {
            Native.countedRefMeasurementDestroy(refHandle);
            throw new SpecUtilsException("measurementPtrFromRef returned null");
        }
        return new Measurement(view, refHandle, parent);
    }

    private Registration registerCleaner() {
        // Capture what this wrapper is responsible for freeing: a reference-counted handle (for
        // measurements from a SpecFile) or, for owned measurements, the measurement itself.
        final long ref = this.refHandle;
        final long ownedHandle = this.ownsHandle ? this.handle : 0L;
        return HandleCleaner.register(cleanerOwner, () -> {
            if (ref != 0L) {
                Native.countedRefMeasurementDestroy(ref);
            } else if (ownedHandle != 0L) {
                Native.measurementDestroy(ownedHandle);
            }
        });
    }

    /**
     * Called by SpecFile.addMeasurement when ownership has been transferred
     * to the parent. Makes {@link #close()} a no-op.
     */
    synchronized void transferOwnership() {
        this.ownsHandle = false;
        if (cleanerRegistration != null) {
            cleanerRegistration.deregister();
            cleanerRegistration = null;
        }
    }

    /** Creates a deep copy, owned by the caller. */
    public Measurement clone_() {
        throwIfClosed();
        long h = Native.measurementClone(handle);
        if (h == 0L) {
            throw new SpecUtilsException("measurementClone returned null");
        }
        return new Measurement(h, true, null);
    }

    public void reset() {
        throwIfClosed();
        Native.measurementReset(handle);
    }

    public int memorySize() {
        throwIfClosed();
        return Native.measurementMemorySize(handle);
    }

    // ----- Metadata getters/setters -----

    public String description()         { throwIfClosed(); return Native.measurementDescription(handle); }
    public void setDescription(String s) { throwIfClosed(); Native.measurementSetDescription(handle, s); }

    public String sourceString()         { throwIfClosed(); return Native.measurementSourceString(handle); }
    public void setSourceString(String s) { throwIfClosed(); Native.measurementSetSourceString(handle, s); }

    public String title()         { throwIfClosed(); return Native.measurementTitle(handle); }
    public void setTitle(String s) { throwIfClosed(); Native.measurementSetTitle(handle, s); }

    public String detectorName()         { throwIfClosed(); return Native.measurementDetectorName(handle); }
    public void setDetectorName(String s) { throwIfClosed(); Native.measurementSetDetectorName(handle, s); }

    public String detectorType() { throwIfClosed(); return Native.measurementDetectorType(handle); }

    public long startTimeMicroseconds() {
        throwIfClosed();
        return Native.measurementStartTimeUsecs(handle);
    }

    public void setStartTimeMicroseconds(long microsecondsSinceEpoch) {
        throwIfClosed();
        Native.measurementSetStartTimeUsecs(handle, microsecondsSinceEpoch);
    }

    /** Throws {@link SpecUtilsException} if the string cannot be parsed. */
    public void setStartTimeString(String startTime) {
        throwIfClosed();
        if (!Native.measurementSetStartTimeStr(handle, startTime)) {
            throw new SpecUtilsException("setStartTimeString: could not parse '"
                + startTime + "'");
        }
    }

    public char pcfTag()        { throwIfClosed(); return Native.measurementPcfTag(handle); }
    public void setPcfTag(char c) { throwIfClosed(); Native.measurementSetPcfTag(handle, c); }

    public int sampleNumber()            { throwIfClosed(); return Native.measurementSampleNumber(handle); }
    public void setSampleNumber(int n)   { throwIfClosed(); Native.measurementSetSampleNumber(handle, n); }

    // ----- Timing & counts -----

    public float realTime()         { throwIfClosed(); return Native.measurementRealTime(handle); }
    public float liveTime()         { throwIfClosed(); return Native.measurementLiveTime(handle); }
    public float neutronLiveTime()  { throwIfClosed(); return Native.measurementNeutronLiveTime(handle); }
    public double gammaCountSum()   { throwIfClosed(); return Native.measurementGammaCountSum(handle); }
    public double neutronCountSum() { throwIfClosed(); return Native.measurementNeutronCountSum(handle); }

    public boolean isOccupied()        { throwIfClosed(); return Native.measurementIsOccupied(handle); }
    public boolean containedNeutron()  { throwIfClosed(); return Native.measurementContainedNeutron(handle); }

    public int numberGammaChannels()   { throwIfClosed(); return Native.measurementNumberGammaChannels(handle); }

    /** @return copy of gamma channel counts; empty array if unset. */
    public float[] gammaChannelCounts() {
        throwIfClosed();
        return Native.measurementGammaChannelCounts(handle);
    }

    /** @return copy of lower channel energies (length = numChannels + 1), empty if unset. */
    public float[] energyBounds() {
        throwIfClosed();
        return Native.measurementEnergyBounds(handle);
    }

    public double gammaIntegral(float lowerEnergy, float upperEnergy) {
        throwIfClosed();
        return Native.measurementGammaIntegral(handle, lowerEnergy, upperEnergy);
    }

    public double gammaChannelsSum(int startBin, int endBin) {
        throwIfClosed();
        return Native.measurementGammaChannelsSum(handle, startBin, endBin);
    }

    public int derivedDataProperties() {
        throwIfClosed();
        return Native.measurementDerivedDataProperties(handle);
    }

    public void setGammaCounts(float[] counts, float liveTime, float realTime) {
        throwIfClosed();
        Native.measurementSetGammaCounts(handle, counts, liveTime, realTime);
    }

    public void setNeutronCounts(float[] counts, float neutronLiveTime) {
        throwIfClosed();
        Native.measurementSetNeutronCounts(handle, counts, neutronLiveTime);
    }

    public boolean combineGammaChannels(int nChannel) {
        throwIfClosed();
        return Native.measurementCombineGammaChannels(handle, nChannel);
    }

    public void rebin(EnergyCalibration cal) {
        throwIfClosed();
        if (cal == null || cal.countedRefHandle == 0L) {
            throw new IllegalArgumentException("rebin requires a counted-ref EnergyCalibration"
                + " (call toCountedRef() on it first)");
        }
        if (!Native.measurementRebin(handle, cal.countedRefHandle)) {
            throw new SpecUtilsException("rebin failed");
        }
    }

    public void setEnergyCalibration(EnergyCalibration cal) {
        throwIfClosed();
        if (cal == null || cal.countedRefHandle == 0L) {
            throw new IllegalArgumentException("setEnergyCalibration requires a counted-ref"
                + " EnergyCalibration (call toCountedRef() on it first)");
        }
        if (!Native.measurementSetEnergyCalibration(handle, cal.countedRefHandle)) {
            throw new SpecUtilsException("measurement setEnergyCalibration failed"
                + " (wrong channel count or invalid cal)");
        }
    }

    /**
     * Non-owning access to this Measurement's energy calibration. Returns
     * null if none is set. The returned wrapper's {@link EnergyCalibration#close}
     * is a no-op — its lifetime follows this Measurement.
     */
    public EnergyCalibration energyCalibration() {
        throwIfClosed();
        long rh = Native.measurementEnergyCalibrationPtr(handle);
        if (rh == 0L) return null;
        return new EnergyCalibration(rh, 0L, false, false);
    }

    /**
     * Returns a counted-ref copy of this measurement's energy calibration,
     * owned by the returned wrapper. Use this to keep the calibration
     * alive independently of the Measurement.
     * @return null if measurement has no calibration.
     */
    public EnergyCalibration energyCalibrationRef() {
        throwIfClosed();
        long crh = Native.measurementEnergyCalibrationRef(handle);
        if (crh == 0L) return null;
        long rh = Native.energyCalPtrFromRef(crh);
        return new EnergyCalibration(rh, crh, false, true);
    }

    // ----- GPS & rates -----

    public boolean hasGpsInfo()    { throwIfClosed(); return Native.measurementHasGpsInfo(handle); }
    public double latitude()       { throwIfClosed(); return Native.measurementLatitude(handle); }
    public double longitude()      { throwIfClosed(); return Native.measurementLongitude(handle); }
    public long positionTimeMicroseconds() {
        throwIfClosed();
        return Native.measurementPositionTimeMicrosec(handle);
    }

    public void setPosition(double longitude, double latitude, long positionTimeMicroseconds) {
        throwIfClosed();
        Native.measurementSetPosition(handle, longitude, latitude, positionTimeMicroseconds);
    }

    public float speed()        { throwIfClosed(); return Native.measurementSpeed(handle); }
    public float doseRate()     { throwIfClosed(); return Native.measurementDoseRate(handle); }
    public float exposureRate() { throwIfClosed(); return Native.measurementExposureRate(handle); }

    // ----- Status enums -----

    public OccupancyStatus occupancyStatus() {
        throwIfClosed();
        return OccupancyStatus.fromOrdinal(Native.measurementOccupancyStatus(handle));
    }

    public void setOccupancyStatus(OccupancyStatus s) {
        throwIfClosed();
        Native.measurementSetOccupancyStatus(handle, s.ordinal());
    }

    public QualityStatus qualityStatus() {
        throwIfClosed();
        return QualityStatus.fromOrdinal(Native.measurementQualityStatus(handle));
    }

    public SourceType sourceType() {
        throwIfClosed();
        return SourceType.fromOrdinal(Native.measurementSourceType(handle));
    }

    public void setSourceType(SourceType s) {
        throwIfClosed();
        Native.measurementSetSourceType(handle, s.ordinal());
    }

    // ----- Remarks & warnings -----

    public int numberRemarks() { throwIfClosed(); return Native.measurementNumberRemarks(handle); }
    public String remark(int index) { throwIfClosed(); return Native.measurementRemark(handle, index); }
    public void setRemarks(String[] remarks) {
        throwIfClosed();
        Native.measurementSetRemarks(handle, remarks);
    }

    public int numberParseWarnings() {
        throwIfClosed();
        return Native.measurementNumberParseWarnings(handle);
    }
    public String parseWarning(int index) {
        throwIfClosed();
        return Native.measurementParseWarning(handle, index);
    }

    @Override
    public synchronized void close() {
        if (handle == 0L) return;
        if (cleanerRegistration != null) {
            cleanerRegistration.deregister();
            cleanerRegistration = null;
        }
        if (refHandle != 0L) {
            // Measurement obtained from a SpecFile: release our reference-counted handle (the
            // underlying measurement is freed once all references and the SpecFile are gone).
            Native.countedRefMeasurementDestroy(refHandle);
        } else if (ownsHandle) {
            Native.measurementDestroy(handle);
        }
        handle = 0L;
        refHandle = 0L;
        ownsHandle = false;
    }

    private void throwIfClosed() {
        if (handle == 0L) {
            throw new IllegalStateException("Measurement has been closed");
        }
    }
}
