package gov.sandia.specutils;

/**
 * Detected or inferred detection system. Ordinal order MUST match the C
 * enum {@code SpecUtils_DetectorType} in {@code SpecUtils_c.h}.
 */
public enum DetectorType {
    Exploranium,
    IdentiFinder,
    IdentiFinderNG,
    IdentiFinderLaBr3,
    IdentiFinderTungsten,
    IdentiFinderR425NaI,
    IdentiFinderR425LaBr,
    IdentiFinderR500NaI,
    IdentiFinderR500LaBr,
    IdentiFinderUnknown,
    DetectiveUnknown,
    DetectiveEx,
    DetectiveEx100,
    DetectiveEx200,
    DetectiveX,
    SAIC8,
    Falcon5000,
    MicroDetective,
    MicroRaider,
    RadiaCodeCsI10,
    RadiaCodeCsI14,
    RadiaCodeGAGG10,
    Raysid,
    Interceptor,
    RadHunterNaI,
    RadHunterLaBr3,
    Rsi701,
    Rsi705,
    AvidRsi,
    OrtecRadEagleNai,
    OrtecRadEagleCeBr2Inch,
    OrtecRadEagleCeBr3Inch,
    OrtecRadEagleLaBr,
    Sam940LaBr3,
    Sam940,
    Sam945,
    Srpm210,
    RIIDEyeNaI,
    RIIDEyeLaBr,
    RadSeekerNaI,
    RadSeekerLaBr,
    VerifinderNaI,
    VerifinderLaBr,
    H3D400,
    KromekD3S,
    KromekD5,
    KromekGR1,
    Fulcrum,
    Fulcrum40h,
    Sam950,
    Unknown;

    public static DetectorType fromOrdinal(int ordinal) {
        DetectorType[] values = values();
        if (ordinal < 0 || ordinal >= values.length) {
            throw new IllegalArgumentException("DetectorType ordinal out of range: " + ordinal);
        }
        return values[ordinal];
    }
}
