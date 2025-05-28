#pragma once

/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

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
    float Energy;
    float Efficiency;
    float EfficiencyUncertainty;
};

struct Peak {
    float Energy;
    float Centroid;
    float CentroidUncertainty;
    float FullWidthAtHalfMaximum;
    float LowTail;
    float Area;
    float AreaUncertainty;
    float Continuum;
    float CriticalLevel;
    float CountRate;
    float CountRateUncertainty;

    Peak() = default;
    Peak(float energy, float centrd, float centrdUnc, float fwhm, float lowTail,
        float area, float areaUnc, float continuum, float critialLevel,
        float cntRate, float cntRateUnc);
};

struct Nuclide {
    std::string Name;
    float HalfLife = 0.;
    float HalfLifeUncertainty = 0.;
    std::string HalfLifeUnit;
    int Index = -1;
    int AtomicNumber = 0;
    std::string ElementSymbol;
    std::string Metastable;

    // all in uCi, default Genie unit
    double Activity = 0.;
    double ActivityUnc = 0.;
    double MDA = 0.;
    

    Nuclide() = default;
    Nuclide(const std::string& name, float halfLife, float halfLifeUnc,
        const std::string& halfLifeUnit, int nucNo,
        double  activity, double activityUnc, double mda);

    inline bool operator==(const Nuclide & other) const
    {
        return Name == other.Name;
    }


};

struct Line {
    float Energy = 0.;
    float EnergyUncertainty = 0.;
    float Abundance = 0.;
    float AbundanceUncertainty = 0.;
    bool IsKeyLine = false;
    int NuclideIndex = -1;
    bool NoWeightMean = false;

    double LineActivity = 0.;
    double LineActivityUnceratinty = 0.;
    float LineEfficiency = 0. ;
    float LineEfficiencyUncertainty = 0.;
    double LineMDA = 0.;

    Line() = default;
    Line(float energy, float energyUnc, float abundance, float abundanceUnc,
        int nucNo, bool key = false, bool noWgtMean = false, double lineAct = 0.,
        double lineActUnc = 0., float lineEff = 0., float lineEffUnc = 0., double lineMDA = 0.);
};

struct DetInfo 
{
    std::string Type; //DETTYPE
    std::string Name;  //DETNAME
    std::string SerialNo; //MCAID
    std::string MCAType; // MCATYPE

    DetInfo() = default;
    DetInfo(std::string type, std::string name, std::string serial_no, std::string mca_type);
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
    std::shared_ptr<std::vector<byte_type>> readData;
    std::vector<byte_type> writebytes;
    std::vector<std::vector<byte_type>> lines;
    std::vector<std::vector<byte_type>> nucs;
    std::vector<byte_type> specData;
    std::vector<Nuclide> writeNuclides; 
    std::vector<Line> fileLines;
    std::vector<Nuclide> fileNuclides;
    std::vector<Peak> filePeaks;
    std::vector<uint32_t> fileSpectrum;
    std::vector<float> fileEneCal;
    std::vector<float> fileShapeCal;

    std::vector<EfficiencyPoint> efficiencyPoints;
    std::vector<Peak> peaks;

    DetInfo det_info;
    uint32_t num_channels =0;

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
    void ReadFile(const std::vector<byte_type>& fileData);

    // get data from a file
    std::vector<Line>& GetLines();
    std::vector<Nuclide>& GetNuclides();
    std::vector<Peak>& GetPeaks();
    std::vector<EfficiencyPoint>& GetEfficiencyPoints();
    SpecUtils::time_point_t GetSampleTime();
    SpecUtils::time_point_t GetAquisitionTime();
    float GetLiveTime();
    float GetRealTime();
    std::vector<float>& GetShapeCalibration();
    std::vector<float>& GetEnergyCalibration();
    std::vector<uint32_t>& GetSpectrum();
    std::string GetSampleTitle();
    DetInfo& GetDetectorInfo();

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
    void AddSampleTitle(const std::string& title);
    void AddGPSData(const double latitude, const double longitude,
        const float speed, const SpecUtils::time_point_t& position_time);
    void AddGPSData(const double latitude, const double longitude, const float speed);
    void AddSpectrum(const std::vector<uint32_t>& channel_counts);
    void AddSpectrum(const std::vector<float>& channel_counts);
    // create a file with the data added
    std::vector<byte_type>& CreateFile();

    inline void SetKeyLineInerferenceLimit(const float limit) { key_line_intf_limit = limit; };
    float GetKeyLineInerferenceLimit() const { return key_line_intf_limit; }

protected:
    std::multimap<CAMBlock, uint32_t> ReadHeader();
    void ReadBlock(CAMBlock block);
    std::vector<uint8_t> GenerateBlock(CAMBlock block, size_t loc, 
                                     const std::vector<std::vector<uint8_t>>& records = std::vector<std::vector<uint8_t>>(),
                                     uint16_t blockNo = 0, bool hasCommon = true);
    std::vector<uint8_t> GenerateBlockHeader(CAMBlock block, size_t loc, uint16_t numRec = 1,
                                           uint16_t numLines = 1, uint16_t blockNum = 0, bool hasCommon = false) const;
    uint16_t GetNumLines(const std::vector<uint8_t>& nuclRecord);
    std::vector<uint8_t> GenerateNuclide(const Nuclide nuc,
                                        const std::vector<uint16_t>& lineNums);
    std::vector<uint8_t> AddLinesToNuclide(const std::vector<uint8_t>& nuc, 
                                          const std::vector<uint8_t>& lineNums);
    std::vector<uint8_t> GenerateLine(const Line line);
    void AssignKeyLines(); 

protected:
    // Add block reading function declarations
    void ReadGeometryBlock(size_t pos, uint16_t records);
    void ReadLinesBlock(size_t pos, uint16_t records);
    void ReadNuclidesBlock(size_t pos, uint16_t records);
    void ReadPeaksBlock(size_t pos, uint16_t records);

    // Add GenerateFile declaration
    void GenerateFile(const std::vector<std::vector<uint8_t>>& blocks);

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