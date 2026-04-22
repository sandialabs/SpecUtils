package gov.sandia.specutils;

import gov.sandia.specutils.internal.HandleCleaner;
import gov.sandia.specutils.internal.HandleCleaner.Registration;
import gov.sandia.specutils.internal.Native;

/**
 * Mapping between gamma channel indices and energies (keV). Wraps the C
 * {@code SpecUtils::EnergyCalibration} object.
 *
 * <p>An EnergyCalibration can be in one of three states internally:
 * <ol>
 *   <li><b>Raw-owned</b>: created via {@link #EnergyCalibration()} and
 *       configured via {@code setPolynomial} / {@code setFullRangeFraction}
 *       / {@code setLowerChannelEnergy}. {@link #close()} destroys the
 *       underlying {@code SpecUtils_EnergyCal}.</li>
 *   <li><b>Counted-ref-owned</b>: obtained via
 *       {@link Measurement#energyCalibrationRef()} or by promoting a
 *       raw-owned calibration via {@link #toCountedRef()}. {@link #close()}
 *       decrements the shared_ptr.</li>
 *   <li><b>Borrowed</b>: obtained via
 *       {@link Measurement#energyCalibration()} — the owning Measurement
 *       holds the lifetime and {@link #close()} is a no-op.</li>
 * </ol>
 *
 * Read methods ({@link #type()}, {@link #coefficients()}, etc.) work on
 * all three.
 *
 * <p>To attach this calibration to a {@link Measurement}, call
 * {@link #toCountedRef()} to promote to a shared_ptr, then pass it to
 * {@link Measurement#setEnergyCalibration(EnergyCalibration)} or
 * {@link SpecFile#setMeasurementEnergyCalibration(EnergyCalibration, Measurement)}.
 */
public final class EnergyCalibration implements AutoCloseable {

    static { Native.init(); }

    /** Underlying {@code SpecUtils_EnergyCal *}. Never 0 for a usable instance. */
    long rawHandle;

    /**
     * {@code SpecUtils_CountedRef_EnergyCal *}, or 0 if this wrapper
     * manages a raw EnergyCal. When non-zero, {@link #rawHandle} is a
     * non-owning pointer derived from this counted ref.
     */
    long countedRefHandle;

    private boolean ownsRaw;
    private boolean ownsCountedRef;
    private final Registration cleanerRegistration;

    // The Object the cleaner watches — once this wrapper becomes unreachable,
    // the cleaner fires. We capture handles into final locals so the lambda
    // doesn't re-read mutable fields after the owner is gone.
    private final Object cleanerOwner = new Object();

    /** Creates a new owned, uninitialized EnergyCalibration. */
    public EnergyCalibration() {
        this.rawHandle = Native.energyCalCreate();
        if (this.rawHandle == 0L) {
            throw new SpecUtilsException("energyCalCreate returned null");
        }
        this.countedRefHandle = 0L;
        this.ownsRaw = true;
        this.ownsCountedRef = false;
        this.cleanerRegistration = registerCleaner();
    }

    /** Internal: wraps an existing handle. */
    EnergyCalibration(long rawHandle, long countedRefHandle,
                      boolean ownsRaw, boolean ownsCountedRef) {
        this.rawHandle = rawHandle;
        this.countedRefHandle = countedRefHandle;
        this.ownsRaw = ownsRaw;
        this.ownsCountedRef = ownsCountedRef;
        this.cleanerRegistration = registerCleaner();
    }

    private Registration registerCleaner() {
        final long rh = this.rawHandle;
        final long crh = this.countedRefHandle;
        final boolean oRaw = this.ownsRaw;
        final boolean oCounted = this.ownsCountedRef;
        return HandleCleaner.register(cleanerOwner, () -> {
            if (oCounted && crh != 0L) {
                Native.countedRefEnergyCalDestroy(crh);
            }
            if (oRaw && rh != 0L) {
                Native.energyCalDestroy(rh);
            }
        });
    }

    /**
     * Promotes a raw-owned EnergyCalibration to a counted-ref form (a
     * {@code std::shared_ptr}). After this call the wrapper manages a
     * counted ref; {@link #close()} decrements the shared_ptr.
     *
     * <p>Required before passing to {@link Measurement#setEnergyCalibration}.
     */
    public synchronized void toCountedRef() {
        throwIfClosed();
        if (countedRefHandle != 0L) {
            return; // already a counted ref
        }
        if (!ownsRaw) {
            throw new SpecUtilsException("toCountedRef requires owned raw EnergyCal");
        }
        long crh = Native.energyCalMakeCountedRef(rawHandle);
        if (crh == 0L) {
            throw new SpecUtilsException("energyCalMakeCountedRef returned null");
        }
        // After make_counted_ref, the counted ref manages the raw EnergyCal.
        // We still track rawHandle for read access but no longer own it.
        this.countedRefHandle = crh;
        this.ownsRaw = false;
        this.ownsCountedRef = true;
        // Re-derive the raw pointer from the ref — in the current C API it
        // is the same pointer, but this keeps the invariant explicit.
        this.rawHandle = Native.energyCalPtrFromRef(crh);
    }

    public EnergyCalType type() {
        throwIfClosed();
        return EnergyCalType.fromOrdinal(Native.energyCalType(rawHandle));
    }

    public boolean valid() {
        throwIfClosed();
        return Native.energyCalValid(rawHandle);
    }

    public int numberCoefficients() {
        throwIfClosed();
        return Native.energyCalNumberCoefficients(rawHandle);
    }

    public float[] coefficients() {
        throwIfClosed();
        return Native.energyCalCoefficients(rawHandle);
    }

    public int numberDeviationPairs() {
        throwIfClosed();
        return Native.energyCalNumberDeviationPairs(rawHandle);
    }

    public float deviationEnergy(int pairIndex) {
        throwIfClosed();
        return Native.energyCalDeviationEnergy(rawHandle, pairIndex);
    }

    public float deviationOffset(int pairIndex) {
        throwIfClosed();
        return Native.energyCalDeviationOffset(rawHandle, pairIndex);
    }

    public int numberChannels() {
        throwIfClosed();
        return Native.energyCalNumberChannels(rawHandle);
    }

    public float[] channelEnergies() {
        throwIfClosed();
        return Native.energyCalChannelEnergies(rawHandle);
    }

    /** @param devPairs flat array of [e0, off0, e1, off1, ...]; may be null. */
    public void setPolynomial(int numChannels, float[] coeffs, float[] devPairs) {
        throwIfClosed();
        if (!ownsRaw) {
            throw new SpecUtilsException("setPolynomial requires owned raw EnergyCal");
        }
        if (!Native.energyCalSetPolynomial(rawHandle, numChannels, coeffs, devPairs)) {
            throw new SpecUtilsException("energyCalSetPolynomial failed (invalid input)");
        }
    }

    public void setFullRangeFraction(int numChannels, float[] coeffs, float[] devPairs) {
        throwIfClosed();
        if (!ownsRaw) {
            throw new SpecUtilsException("setFullRangeFraction requires owned raw EnergyCal");
        }
        if (!Native.energyCalSetFullRangeFraction(rawHandle, numChannels, coeffs, devPairs)) {
            throw new SpecUtilsException("energyCalSetFullRangeFraction failed");
        }
    }

    public void setLowerChannelEnergy(int numChannels, float[] channelEnergies) {
        throwIfClosed();
        if (!ownsRaw) {
            throw new SpecUtilsException("setLowerChannelEnergy requires owned raw EnergyCal");
        }
        if (!Native.energyCalSetLowerChannelEnergy(rawHandle, numChannels, channelEnergies)) {
            throw new SpecUtilsException("energyCalSetLowerChannelEnergy failed");
        }
    }

    public double channelForEnergy(double energy) {
        throwIfClosed();
        return Native.energyCalChannelForEnergy(rawHandle, energy);
    }

    public double energyForChannel(double channel) {
        throwIfClosed();
        return Native.energyCalEnergyForChannel(rawHandle, channel);
    }

    public float lowerEnergy() {
        throwIfClosed();
        return Native.energyCalLowerEnergy(rawHandle);
    }

    public float upperEnergy() {
        throwIfClosed();
        return Native.energyCalUpperEnergy(rawHandle);
    }

    @Override
    public synchronized void close() {
        if (rawHandle == 0L && countedRefHandle == 0L) {
            return;
        }
        cleanerRegistration.deregister(); // don't double-free
        if (ownsCountedRef && countedRefHandle != 0L) {
            Native.countedRefEnergyCalDestroy(countedRefHandle);
        }
        if (ownsRaw && rawHandle != 0L) {
            Native.energyCalDestroy(rawHandle);
        }
        rawHandle = 0L;
        countedRefHandle = 0L;
        ownsRaw = false;
        ownsCountedRef = false;
    }

    private void throwIfClosed() {
        if (rawHandle == 0L) {
            throw new IllegalStateException("EnergyCalibration has been closed");
        }
    }
}
