package gov.sandia.specutils;

/**
 * Portal-monitor occupancy state for a Measurement. Ordinal order MUST
 * match {@code SpecUtils_OccupancyStatus} in {@code SpecUtils_c.h}.
 */
public enum OccupancyStatus {
    NotOccupied,
    Occupied,
    Unknown;

    public static OccupancyStatus fromOrdinal(int ordinal) {
        OccupancyStatus[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("OccupancyStatus ordinal out of range: " + ordinal);
        }
        return values[ordinal];
    }
}
