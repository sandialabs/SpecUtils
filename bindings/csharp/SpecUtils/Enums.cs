namespace SpecUtils;

/// <summary>
/// Spectrum file format types for parsing.
/// Corresponds to the C++ enum SpecUtils::ParserType.
/// </summary>
public enum ParserType
{
    N42_2006 = 0,
    N42_2012 = 1,
    Spc = 2,
    Exploranium = 3,
    Pcf = 4,
    Chn = 5,
    SpeIaea = 6,
    TxtOrCsv = 7,
    Cnf = 8,
    TracsMps = 9,
    Aram = 10,
    SPMDailyFile = 11,
    AmptekMca = 12,
    MicroRaider = 13,
    RadiaCode = 14,
    OrtecListMode = 15,
    LsrmSpe = 16,
    Tka = 17,
    MultiAct = 18,
    Phd = 19,
    Lzs = 20,
    ScanDataXml = 21,
    Json = 22,
    CaenHexagonGXml = 23,

    /// <summary>Requires SpecUtils_ENABLE_URI_SPECTRA compile flag.</summary>
    Uri = 24,

    Auto = 25
}

/// <summary>
/// Spectrum file format types for saving/exporting.
/// Corresponds to the C++ enum SpecUtils::SaveSpectrumAsType.
/// </summary>
public enum SaveSpectrumAsType
{
    Txt = 0,
    Csv = 1,
    Pcf = 2,
    N42_2006 = 3,
    N42_2012 = 4,
    Chn = 5,
    SpcBinaryInt = 6,
    SpcBinaryFloat = 7,
    SpcAscii = 8,
    ExploraniumGr130v0 = 9,
    ExploraniumGr135v2 = 10,
    SpeIaea = 11,
    Cnf = 12,
    Tka = 13,

    /// <summary>Requires SpecUtils_ENABLE_D3_CHART compile flag.</summary>
    HtmlD3 = 14,

    /// <summary>Requires SpecUtils_INJA_TEMPLATES compile flag.</summary>
    Template = 15,

    /// <summary>Requires SpecUtils_ENABLE_URI_SPECTRA compile flag.</summary>
    Uri = 16,

    NumTypes = 17
}

/// <summary>
/// Detector system type, inferred or specified from the spectrum file.
/// Corresponds to the C++ enum SpecUtils::DetectorType.
/// </summary>
public enum DetectorType
{
    Exploranium = 0,
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
    KromekD3S,
    KromekD5,
    KromekGR1,
    Fulcrum,
    Fulcrum40h,
    Sam950,
    Unknown
}

/// <summary>
/// Source type of a measurement.
/// </summary>
public enum SourceType
{
    IntrinsicActivity = 0,
    Calibration = 1,
    Background = 2,
    Foreground = 3,
    Unknown = 4
}

/// <summary>
/// Occupancy status of a measurement.
/// </summary>
public enum OccupancyStatus
{
    NotOccupied = 0,
    Occupied = 1,
    Unknown = 2
}

/// <summary>
/// Quality status of a measurement.
/// </summary>
public enum QualityStatus
{
    Good = 0,
    Suspect = 1,
    Bad = 2,
    Missing = 3
}

/// <summary>
/// Energy calibration equation type.
/// </summary>
public enum EnergyCalType
{
    Polynomial = 0,
    FullRangeFraction = 1,
    LowerChannelEdge = 2,
    UnspecifiedUsingDefaultPolynomial = 3,
    InvalidEquationType = 4
}
