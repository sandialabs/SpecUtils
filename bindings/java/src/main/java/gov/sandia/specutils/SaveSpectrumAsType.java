package gov.sandia.specutils;

/**
 * Output format when writing a SpecFile. Ordinal order MUST match the C
 * enum {@code SpecUtils_SaveSpectrumAsType} in {@code SpecUtils_c.h}.
 *
 * <p>{@code HtmlD3}, {@code Template}, and {@code Uri} require the
 * corresponding CMake option at library build time; using them without the
 * feature enabled will throw {@link SpecUtilsException}.
 */
public enum SaveSpectrumAsType {
    Txt,
    Csv,
    Pcf,
    N42_2006,
    N42_2012,
    Chn,
    SpcBinaryInt,
    SpcBinaryFloat,
    SpcAscii,
    ExploraniumGr130v0,
    ExploraniumGr135v2,
    SpeIaea,
    Cnf,
    Tka,
    HtmlD3,
    Template,
    Uri,
    NumTypes;

    public static SaveSpectrumAsType fromOrdinal(int ordinal) {
        SaveSpectrumAsType[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("SaveSpectrumAsType ordinal out of range: "
                + ordinal);
        }
        return values[ordinal];
    }
}
