using System.Runtime.InteropServices;

namespace SpecUtils;

/// <summary>
/// Represents an energy calibration for a gamma spectrum.
/// May be owned (caller must dispose) or borrowed (lifetime tied to a Measurement).
/// </summary>
public class EnergyCalibration : IDisposable
{
    internal IntPtr Handle;
    private bool _ownsHandle;
    private bool _disposed;

    /// <summary>Creates a new empty owned EnergyCalibration.</summary>
    public EnergyCalibration()
    {
        Handle = NativeMethods.SpecUtils_EnergyCal_create();
        _ownsHandle = true;
    }

    internal EnergyCalibration(IntPtr handle, bool ownsHandle)
    {
        Handle = handle;
        _ownsHandle = ownsHandle;
    }

    /// <summary>Called when ownership is transferred (e.g., to a CountedRefEnergyCalibration).</summary>
    internal void TransferOwnership()
    {
        _ownsHandle = false;
    }

    // ---- Properties ----

    public EnergyCalType Type
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_type(Handle); }
    }

    public bool IsValid
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_valid(Handle); }
    }

    public uint NumberCoefficients
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_number_coefficients(Handle); }
    }

    public uint NumberDeviationPairs
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_number_deviation_pairs(Handle); }
    }

    public uint NumberChannels
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_number_channels(Handle); }
    }

    public float LowerEnergy
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_lower_energy(Handle); }
    }

    public float UpperEnergy
    {
        get { ThrowIfDisposed(); return NativeMethods.SpecUtils_EnergyCal_upper_energy(Handle); }
    }

    // ---- Data access ----

    /// <summary>Returns a copy of the calibration coefficients.</summary>
    public float[]? GetCoefficients()
    {
        ThrowIfDisposed();
        uint count = NativeMethods.SpecUtils_EnergyCal_number_coefficients(Handle);
        if (count == 0) return null;

        IntPtr ptr = NativeMethods.SpecUtils_EnergyCal_coefficients(Handle);
        if (ptr == IntPtr.Zero) return null;

        float[] result = new float[count];
        Marshal.Copy(ptr, result, 0, (int)count);
        return result;
    }

    /// <summary>
    /// Returns a copy of the channel energies array.
    /// Has NumberChannels + 1 entries.
    /// </summary>
    public float[]? GetChannelEnergies()
    {
        ThrowIfDisposed();
        uint count = NativeMethods.SpecUtils_EnergyCal_number_channels(Handle);
        if (count == 0) return null;

        IntPtr ptr = NativeMethods.SpecUtils_EnergyCal_channel_energies(Handle);
        if (ptr == IntPtr.Zero) return null;

        float[] result = new float[count + 1];
        Marshal.Copy(ptr, result, 0, (int)(count + 1));
        return result;
    }

    public float GetDeviationEnergy(int index)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_EnergyCal_deviation_energy(Handle, (uint)index);
    }

    public float GetDeviationOffset(int index)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_EnergyCal_deviation_offset(Handle, (uint)index);
    }

    /// <summary>Returns the fractional channel number for a given energy.</summary>
    public double ChannelForEnergy(double energy)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_EnergyCal_channel_for_energy(Handle, energy);
    }

    /// <summary>Returns the energy for a given fractional channel number.</summary>
    public double EnergyForChannel(double channel)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_EnergyCal_energy_for_channel(Handle, channel);
    }

    // ---- Setters ----

    /// <summary>
    /// Sets polynomial energy calibration coefficients.
    /// </summary>
    /// <param name="numChannels">Number of channels this calibration applies to.</param>
    /// <param name="coefficients">Polynomial coefficients (at least 2).</param>
    /// <param name="deviationPairs">Optional deviation pairs as [energy0, offset0, energy1, offset1, ...].</param>
    /// <returns>True if the calibration is valid.</returns>
    public bool SetPolynomial(uint numChannels, float[] coefficients, float[]? deviationPairs = null)
    {
        ThrowIfDisposed();
        uint numDevPairs = deviationPairs != null ? (uint)(deviationPairs.Length / 2) : 0;
        return NativeMethods.SpecUtils_EnergyCal_set_polynomial(
            Handle, numChannels, coefficients, (uint)coefficients.Length,
            deviationPairs, numDevPairs);
    }

    /// <summary>
    /// Sets full-range-fraction energy calibration coefficients.
    /// </summary>
    public bool SetFullRangeFraction(uint numChannels, float[] coefficients, float[]? deviationPairs = null)
    {
        ThrowIfDisposed();
        uint numDevPairs = deviationPairs != null ? (uint)(deviationPairs.Length / 2) : 0;
        return NativeMethods.SpecUtils_EnergyCal_set_full_range_fraction(
            Handle, numChannels, coefficients, (uint)coefficients.Length,
            deviationPairs, numDevPairs);
    }

    /// <summary>
    /// Sets lower-channel-edge energy calibration.
    /// </summary>
    public bool SetLowerChannelEnergy(uint numChannels, float[] channelEnergies)
    {
        ThrowIfDisposed();
        return NativeMethods.SpecUtils_EnergyCal_set_lower_channel_energy(
            Handle, numChannels, (uint)channelEnergies.Length, channelEnergies);
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
                NativeMethods.SpecUtils_EnergyCal_destroy(Handle);
            }
            Handle = IntPtr.Zero;
            _disposed = true;
        }
    }

    ~EnergyCalibration()
    {
        Dispose(false);
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }
}

/// <summary>
/// A reference-counted wrapper around EnergyCalibration, allowing multiple measurements
/// to share the same calibration. Wraps std::shared_ptr&lt;const SpecUtils::EnergyCalibration&gt;.
/// </summary>
public class CountedRefEnergyCalibration : IDisposable
{
    internal IntPtr Handle;
    private bool _disposed;

    /// <summary>Creates a new empty counted reference.</summary>
    public CountedRefEnergyCalibration()
    {
        Handle = NativeMethods.SpecUtils_CountedRef_EnergyCal_create();
    }

    internal CountedRefEnergyCalibration(IntPtr handle)
    {
        Handle = handle;
    }

    /// <summary>
    /// Creates a counted reference from an owned EnergyCalibration.
    /// Ownership of the EnergyCalibration is transferred; do not dispose it afterwards.
    /// </summary>
    public static CountedRefEnergyCalibration FromEnergyCalibration(EnergyCalibration cal)
    {
        IntPtr refPtr = NativeMethods.SpecUtils_EnergyCal_make_counted_ref(cal.Handle);
        cal.TransferOwnership();
        return new CountedRefEnergyCalibration(refPtr);
    }

    /// <summary>
    /// Gets the EnergyCalibration owned by this counted reference (borrowed, do not dispose).
    /// </summary>
    public EnergyCalibration? GetEnergyCalibration()
    {
        ThrowIfDisposed();
        IntPtr ptr = NativeMethods.SpecUtils_EnergyCal_ptr_from_ref(Handle);
        return ptr == IntPtr.Zero ? null : new EnergyCalibration(ptr, ownsHandle: false);
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
            if (Handle != IntPtr.Zero)
            {
                NativeMethods.SpecUtils_CountedRef_EnergyCal_destroy(Handle);
                Handle = IntPtr.Zero;
            }
            _disposed = true;
        }
    }

    ~CountedRefEnergyCalibration()
    {
        Dispose(false);
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }
}
