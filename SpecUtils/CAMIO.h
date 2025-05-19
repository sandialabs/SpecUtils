#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <memory>
#include <chrono>
#include "DateTime.h"

namespace 
{
    typedef unsigned char byte_type;
}

namespace CAMInputOutput 
{

// Structs
struct EfficiencyPoint {
    int Index;
    double Energy;
    double Efficiency;
    double EfficiencyUncertainty;
};

struct Peak {
    double Energy;
    double Centroid;
    double CentroidUncertainty;
    double FullWidthAtHalfMaximum;
    double LowTail;
    double Area;
    double AreaUncertainty;
    double Continuum;
    double CriticalLevel;
    double CountRate;
    double CountRateUncertainty;

    Peak() = default;
    Peak(double energy, double centrd, double centrdUnc, double fwhm, double lowTail, 
         double area, double areaUnc, double continuum, double critialLevel, 
         double cntRate, double cntRateUnc);
};

struct Nuclide {
    std::string Name;
    double HalfLife = 0.;
    double HalfLifeUncertainty = 0.;
    std::string HalfLifeUnit;
    int Index = -1;
    int AtomicNumber = 0;
    std::string ElementSymbol;
    std::string Metastable;

    Nuclide() = default;
    Nuclide(const std::string& name, double halfLife, double halfLifeUnc, 
            const std::string& halfLifeUnit, int nucNo);

    inline bool operator==(const Nuclide & other) const
    {
        return Name == other.Name;
    }


};

struct Line {
    double Energy;
    double EnergyUncertainty;
    double Abundance;
    double AbundanceUncertainty;
    bool IsKeyLine;
    int NuclideIndex;
    bool NoWeightMean;

    Line() = default;
    Line(double energy, double energyUnc, double abundance, double abundanceUnc, 
         int nucNo, bool key = false, bool noWgtMean = false);
};

// Main CAMIO class
class CAMIO {
public:
    enum class CAMBlock : uint32_t {
        ACQP = 0x00012000,
        SAMP = 0x00012001,
        GEOM = 0x00012002,
        PROC = 0x00012003,
        DISP = 0x00012004,
        SPEC = 0x00012005, //also known as DATA
        PEAK = 0x00012006,
        NUCL = 0x00012007,
        NLINES = 0x00012008
    };

    enum class RecordSize : uint16_t {
        ACQP = 0x0440,
        NUCL = 0x023B,
        NLINES = 0x0085
    };

    enum class BlockSize : uint16_t {
        ACQP = 0x800,
        PROC = 0x800,
        NUCL = 0x4800,
        NLINES = 0x4200,
        SAMP = 0x0A00
    };

    enum class PeakParameterLocation : uint8_t {
        Energy = 0x0,
        Centroid = 0x40,
        CentroidUncertainty = 0x40,
        FullWidthAtHalfMaximum = 0x10,
        LowTail = 0x50,
        Area = 0x34,
        AreaUncertainty = 0x84,
        Continuum = 0x0C,
        CriticalLevel = 0x0D1,
        CountRate = 0x18,
        CountRateUncertainty = 0x1C
    };

    enum class EfficiencyPointParameterLocation : uint8_t {
        Energy = 0x01,
        Efficiency = 0x05,
        EfficiencyUncertainty = 0x09
    };

    enum NuclideParameterLocation : uint8_t
    {
        Name = 0x03,
        HalfLife = 0x1B,
        HalfLifeUncertainty = 0x89,
        HalfLifeUnit = 0x61,
        MeanActivity = 0x57,
        MeanActivityUnceratinty = 0x69,
        NuclideMDA = 0x27
    };

    enum LineParameterLocation : uint8_t
    {
        Energy = 0x01,
        EnergyUncertainty = 0x21,
        Abundance = 0x05,
        AbundanceUncertainty = 0x39,
        IsKeyLine = 0x1D,
        NuclideIndex = 0x1B,
        NoWeightMean = 0x1F,
        LineActivity = 0x0B,
        LineActivityUnceratinty = 0x13,
        LineEfficiency = 0x31,
        LineEfficiencyUncertainty = 0x35,
        LineMDA = 0x25, 
    };

private:
    std::multimap<CAMBlock, uint32_t> blockAddresses;
    std::vector<byte_type> readData;
    std::vector<std::vector<byte_type>> lines;
    std::vector<std::vector<byte_type>> nucs;
    std::vector<byte_type> specData;
    std::vector<Nuclide> writeNuclides; 
    std::vector<Line> fileLines;
    std::vector<Nuclide> fileNuclides;
    std::vector<EfficiencyPoint> efficiencyPoints;
    std::vector<Peak> peaks;

    static constexpr uint16_t header_size = 0x800;
    static constexpr uint16_t block_header_size = 0x30;
    static constexpr uint8_t  nuclide_line_size = 0x03;
    static constexpr size_t file_header_length = 0x800;
    static constexpr size_t sec_header_length = 0x30;
    static constexpr uint16_t acqp_rec_tab_loc = 0x01FB;

    float key_line_intf_limit = 2.0; //keV
    bool sampBlock = false;
    bool specBlock = false;

public:
    CAMIO();
    void ReadFile(const std::string& fileName);

    // get data from a file
    std::vector<Line> GetLines();
    std::vector<Nuclide> GetNuclides();
    std::vector<Peak> GetPeaks();
    std::vector<EfficiencyPoint> GetEfficiencyPoints();
    SpecUtils::time_point_t GetSampleTime();
    SpecUtils::time_point_t GetAquisitionTime();
    float GetLiveTime();
    float GetRealTime();
    std::vector<float> GetShapeCalibration();
    std::vector<float> GetEnergyCalibration();
    std::vector<uint32_t> GetSpectrum();

    // add data to CAMIO object for later file writing
    void AddNuclide(const std::string& name, const float halfLife, 
        const float halfLifeUnc, const std::string& halfLifeUnit, const int nucNo = -1);
    void AddNuclide(const Nuclide& nuc);
    void AddLine(const float energy, const float enUnc, const float yield, 
        const float yieldUnc, const int nucNo, const bool key = false);
    void AddLine(const Line& line);
    void AddLineAndNuclide(const float energy,  const float yield, const std::string& name, 
        const float halfLife, const std::string& halfLifeUnit, const bool noWeightMean = false,
        const float enUnc = -1, const float yieldUnc = -1, const float halfLifeUnc = -1 );
    void AddEnergyCalibration(const std::vector<float> coefficients);
    void AddDetectorType(const std::string& detector_type);
    void AddAcquitionTime(const SpecUtils::time_point_t& start_time);
    void AddRealTime(const float real_time);
    void AddLiveTime(const float live_time);
    void AddSampleTitle();
    void AddGPSData(const double latitude, const double longitude,
        const float speed, const SpecUtils::time_point_t& position_time);
    void AddSpectrum(const std::vector<uint32_t>& channel_counts);

    // create a file with the data added
    std::vector<byte_type> CreateFile(const std::string& filePath);

    inline void SetKeyLineInerferenceLimit(const float limit) { key_line_intf_limit = limit; };
    float GetKeyLineInerferenceLimit() const { return key_line_intf_limit; }

protected:
    std::multimap<CAMBlock, uint32_t> ReadHeader();
    void ReadBlock(CAMBlock block);
    std::vector<uint8_t> GenerateBlock(CAMBlock block, size_t loc, 
                                     const std::vector<std::vector<uint8_t>>& records = std::vector<std::vector<uint8_t>>(),
                                     uint16_t blockNo = 0, bool hasCommon = true);
    std::vector<uint8_t> GenerateBlockHeader(CAMBlock block, size_t loc, uint16_t numRec = 1,
                                           uint16_t numLines = 1, uint16_t blockNum = 0, bool hasCommon = false);
    uint16_t GetNumLines(const std::vector<uint8_t>& nuclRecord);
    std::vector<uint8_t> GenerateNuclide(const std::string& name, float halfLife, 
                                        float halfLifeUnc, const std::string& halfLifeUnit,
                                        const std::vector<uint16_t>& lineNums);
    std::vector<uint8_t> AddLinesToNuclide(const std::vector<uint8_t>& nuc, 
                                          const std::vector<uint8_t>& lineNums);
    std::vector<uint8_t> GenerateLine(float energy, float enUnc, float yield,
                                     float yieldUnc, bool key, uint8_t nucNo, bool noWgtMn);
    void AssignKeyLines(); 

protected:
    // Add block reading function declarations
    void ReadGeometryBlock(size_t pos, uint16_t records);
    void ReadLinesBlock(size_t pos, uint16_t records);
    void ReadNuclidesBlock(size_t pos, uint16_t records);
    void ReadPeaksBlock(size_t pos, uint16_t records);

    // Add GenerateFile declaration
    std::vector<uint8_t> GenerateFile(const std::vector<std::vector<uint8_t>>& blocks);

    float ComputeUncertainty(float value);
};

// Helper class for comparing lines
class LineComparer {
public:
    bool operator()(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) const;
};

class NuclideComparer {
public:
    bool operator()(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) const;
};

} // namespace CAMInputOutput 