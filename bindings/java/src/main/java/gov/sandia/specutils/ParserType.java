package gov.sandia.specutils;

/**
 * Input spectrum-file format to parse. Order MUST match the C enum
 * {@code SpecUtils_ParserType} in {@code bindings/c/SpecUtils_c.h} because
 * ordinal values cross the JNI boundary.
 *
 * <p>Note: {@code Uri} is only available when SpecUtils was built with
 * {@code SpecUtils_ENABLE_URI_SPECTRA=ON}. Using it against a library built
 * without URI support will throw {@link SpecUtilsException}.
 */
public enum ParserType {
    N42_2006,
    N42_2012,
    Spc,
    Exploranium,
    Pcf,
    Chn,
    SpeIaea,
    TxtOrCsv,
    Cnf,
    TracsMps,
    Aram,
    SPMDailyFile,
    AmptekMca,
    MicroRaider,
    RadiaCode,
    OrtecListMode,
    LsrmSpe,
    Tka,
    MultiAct,
    Phd,
    Lzs,
    ScanDataXml,
    Json,
    CaenHexagonGXml,
    Uri,
    Auto;

    public static ParserType fromOrdinal(int ordinal) {
        ParserType[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("ParserType ordinal out of range: " + ordinal);
        }
        return values[ordinal];
    }
}
