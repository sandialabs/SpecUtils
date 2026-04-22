package gov.sandia.specutils;

/**
 * Measurement self-reported quality. Ordinal order MUST match
 * {@code SpecUtils_QualityStatus} in {@code SpecUtils_c.h}.
 */
public enum QualityStatus {
    Good,
    Suspect,
    Bad,
    Missing;

    public static QualityStatus fromOrdinal(int ordinal) {
        QualityStatus[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("QualityStatus ordinal out of range: " + ordinal);
        }
        return values[ordinal];
    }
}
