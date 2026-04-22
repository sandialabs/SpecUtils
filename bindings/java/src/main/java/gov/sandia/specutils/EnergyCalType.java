package gov.sandia.specutils;

/**
 * Form of the channel-to-energy mapping for an EnergyCalibration. Ordinal
 * order MUST match {@code SpecUtils_EnergyCalType} in {@code SpecUtils_c.h}.
 */
public enum EnergyCalType {
    Polynomial,
    FullRangeFraction,
    LowerChannelEdge,
    UnspecifiedUsingDefaultPolynomial,
    InvalidEquationType;

    public static EnergyCalType fromOrdinal(int ordinal) {
        EnergyCalType[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("EnergyCalType ordinal out of range: " + ordinal);
        }
        return values[ordinal];
    }
}
