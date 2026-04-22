package gov.sandia.specutils;

/**
 * What the Measurement represents (background, foreground, calibration
 * source, etc.). Ordinal order MUST match {@code SpecUtils_SourceType} in
 * {@code SpecUtils_c.h}.
 */
public enum SourceType {
    IntrinsicActivity,
    Calibration,
    Background,
    Foreground,
    Unknown;

    public static SourceType fromOrdinal(int ordinal) {
        SourceType[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("SourceType ordinal out of range: " + ordinal);
        }
        return values[ordinal];
    }
}
