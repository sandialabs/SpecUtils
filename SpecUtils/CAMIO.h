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
#include <optional>
#include <utility>
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

/** A peak-search/fit result, as stored in a CAM file's PEAK block.

 Units were established by decoding a Genie-produced CNF with a populated PEAK block
 (CNFreader's `Examples/cs137.CNF`) against its own reported calibration and ROI:
 energy 661.706 keV, centroid 882.917 channels, FWHM 12.917 (its shape calibration gives
 12.05 keV at that energy - so keV, not channels), net area 2666562 counts over ROI 850-915,
 count rate 360.3458 = area/live-time, and count-rate uncertainty 0.062916 = 100*areaUnc/area.
 */
struct Peak {
    /** Peak energy, in keV. */
    float Energy;
    /** The channel number of the peak. */
    float Centroid;
    /** Uncertainty in `Centroid`, in channels. */
    float CentroidUncertainty;
    /** FWHM in keV (NOT channels). */
    float FullWidthAtHalfMaximum;
    /** Genie's low-tail parameter `T`: how far below the centroid the Gaussian core is stitched
     to an exponential low-energy tail,
        P(x) = H*exp(-0.5*u^2)                      for u >= -T/sigma
        P(x) = H*exp(0.5*(T/sigma)^2 + (T/sigma)*u) for u <  -T/sigma,  u = (x - centroid)/sigma
     i.e. the "GaussExp" function, with `T/sigma` as its shape parameter.

     Units are taken to be keV, to match `FullWidthAtHalfMaximum`; Genie's documentation says
     "channels or energy units" without committing, and every real file seen here stores an
     effectively-infinite value, so this could not be pinned down from data.

     A large value means "no tail"; see `sm_no_low_tail`.
     */
    float LowTail;
    /** Net peak area, in counts. */
    float Area;
    /** Absolute uncertainty on `Area`, in counts. */
    float AreaUncertainty;
    /** Counts in the continuum under the peak's ROI. */
    float Continuum;
    /** Currie critical level, in counts; 0 leaves it for Genie to determine. */
    float CriticalLevel;
    /** Counts per second, i.e. `Area` divided by the live time. */
    float CountRate;
    /** RELATIVE uncertainty on the count rate, as a PERCENT - i.e. `100*AreaUncertainty/Area`,
     not an absolute counts-per-second value.
     */
    float CountRateUncertainty;
    int LeftChannel;
    int RightChannel;

    /** The `LowTail` value a real Genie file uses to mean "no low-energy tail". */
    static constexpr float sm_no_low_tail = 1000.0f;

    Peak() = default;
    Peak(float energy, float centrd, float centrdUnc, float fwhm, float lowTail,
        float area, float areaUnc, float continuum, float critialLevel,
        float cntRate, float cntRateUnc, int leftChan, int rightChan);
};

struct Nuclide {
    std::string Name;
    /** Half-life, expressed in `HalfLifeUnit` - NOT seconds.  The file itself stores seconds; the
     reader divides by the unit and the writer multiplies back, so a value read out of a file can
     be handed straight back to `AddNuclide(...)`.
     */
    float HalfLife = 0.;
    /** Uncertainty on `HalfLife`, in the same units as it. */
    float HalfLifeUncertainty = 0.;
    /** One of "y", "d", "h", "m" or "s" - the unit `HalfLife` is in.  Case-insensitive, and Genie
     pads it with spaces ("Y "), so compare via `half_life_unit_to_seconds()` rather than directly.
     */
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

/** K-edge densitometry (HKED) information stored in CNF files.

 These parameters are used in Hybrid K-Edge Densitometry (HKED) measurements
 for safeguards applications.
 */
struct KEdgeInfo
{
    /** Sample temperature in degrees Celsius (CAM_F_STEMP) */
    float temperature = 0.0f;

    /** Path length/diameter of sample container in cm (CAM_F_KCPATHLEN) */
    float pathLength = 0.0f;

    /** Default declared U-235 enrichment in percent (CAM_F_PRKEDDCL235) */
    float u235Enrichment = 0.0f;

    /** Default declared Pu atomic weight in grams/mole (CAM_F_PRKEDDPUAWT) */
    float puAtomicWeight = 0.0f;

    /** Whether K-edge info was found in the file */
    bool hasInfo = false;

    KEdgeInfo() = default;
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
        NLINES = 0x00012008,

        /** Energy-calibration provenance.  Two blocks that carry the same two strings, in opposite
         order: the calibration method ("Gamma Encal v2.2" when Genie fit it, "Manual Coefs." when
         coefficients were typed in) and, for a manual calibration, THE OPERATOR'S NAME.  Both sit
         at block offset 0x30, 16 bytes each, and both have `numRec` calibration points (0 for a
         manual calibration).  We do not write either block.

         Worth knowing when handling user files: these, and the SAMP block, mean a Genie CNF can
         carry a person's name.
         */
        ENERGY_CAL_METHOD = 0x0001200D,
        ENERGY_CAL_METHOD2 = 0x00012013,

        /** The analysis sequence record: what Genie actually ran, and with what inputs.

         `numRec` 217-byte records, one per analysis step, each naming its engine at record offset
         0x31 - e.g. PLUNID (peak locate + nuclide ID), PANOLIN1 (peak analysis; the same name each
         PEAK record carries at 0x98), ARBACK (background subtract), ECEFCOR (efficiency
         correction), ACINTERF (interference correction), RPSTD (standard report), NID_Intf.

         The block's common area holds the paths those steps used: the analysis template
         (`...\CTLFILES\ANALYSIS.TPL`), the nuclide libraries (`...\CAMFILES\STDLIB.NLB`), the
         background spectrum, and report section names ("HEADER", "PeakEff").  A file that had a
         full analysis run has 5-7 records; one that only had a peak search has none.

         Also a privacy note: these paths are absolute paths off the analyst's machine.
         */
        ANALYSIS_SEQUENCE = 0x00012010,

        K_EDGE_CONFIG = 0x00012024  // K-edge configuration block
    };

    enum class RecordSize : uint16_t {
        ACQP = 0x0440,
        NUCL = 0x023B,
        NLINES = 0x0085,
        //248; from a Genie-produced CNF that actually contains peak records (CNFreader's
        // Examples/cs137.CNF).  Note this is not universal - another real file uses 214 - but
        // both writer and reader take it from the block header, so it only needs to be plausible.
        PEAK = 0x00F8
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
        CentroidUncertainty = 0x44,
        FullWidthAtHalfMaximum = 0x10,
        LowTail = 0x50,
        Area = 0x34,
        AreaUncertainty = 0x84,
        Continuum = 0x0C,
        CriticalLevel = 0x0D1,
        CountRate = 0x18,
        CountRateUncertainty = 0x1C,
        LeftChannel = 0x06,
        Width = 0x0A
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

    enum class PeakParameterLocation2 : uint8_t {
        /** 0x10 for a peak fitted alone in its ROI, 0xD8 for one of several peaks sharing a ROI.
         Holds without exception across every peak of every Genie file checked.
         */
        MultipletFlag = 0x29,
        /** A verbatim second copy of `Area` (0x34); Genie writes both on every peak. */
        AreaAgain = 0x8C,
        /** A verbatim second copy of `AreaUncertainty` (0x84). */
        AreaUncertaintyAgain = 0x90,
        /** Name of the fit engine that produced the peak, e.g. "PANOLIN1S"; 16 bytes. */
        FitEngineName = 0x98,
    };

    enum class EfficiencyModel : uint8_t
    {
      SPLINE, EMPIRICAL, AVERAGE, DUAL, LINEAR,
      /** Genie's tabulated/interpolated efficiency; seen in real Genie-written GEOM blocks. */
      INTERPOL,
      Unknown, NotReadin
    };

    //enum class FwhmType : uint8_t
    //{
    //  CONSTANT, SQRT, Unknown, NotReadin
    //};

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

    //FwhmType fwhmType;
    std::vector<float> fileShapeCal;

    EfficiencyModel efficiencyModel; //Defaults to EfficiencyModel::Unknown;
    std::vector<EfficiencyPoint> efficiencyPoints;
    std::vector<Peak> peaks;

    // Data staged for writing a GEOM (efficiency) block; see `AddEfficiencyModel`/`AddEfficiencyPoint(s)`.
    EfficiencyModel writeEfficiencyModel = EfficiencyModel::Unknown;
    std::vector<EfficiencyPoint> writeEfficiencyPoints;

    // Data staged for writing a PEAK block; see `AddPeak(s)`.
    std::vector<Peak> writePeaks;

    /** Timestamp written as when the peak search/fit was performed; see `GeneratePeakBlock()`.
     Set from `AddAcquitionTime(...)` so writing a file stays deterministic (a real Genie file
     would carry the time analysis was actually run, which is a little later).
     */
    SpecUtils::time_point_t analysisTime{};

    /** The ACQP and SAMP blocks' "common" sections, built up by the various `Add...(...)`
     functions and written out by `CreateFile()`.

     Note: these were previously file-scope variables shared by every `CAMIO`, which leaked
     state between successive writes (a second file inherited the first one's energy
     calibration, detector type, sample title, ...) and made concurrent writes race.
     */
    std::vector<byte_type> acqpCommon;
    std::vector<byte_type> sampCommon;

    DetInfo det_info;
    uint32_t num_channels =0;

    static constexpr uint16_t header_size = 0x800;
    static constexpr uint16_t block_header_size = 0x30;

    /** The most blocks a CAM file's block directory can describe.

     The directory occupies 0x70 up to the first block at 0x800 in 0x30-byte entries (40 would
     fit), but `ReadHeader()` only scans 28 - so 28 is the real, round-trippable limit.
     */
    static constexpr size_t sm_max_blocks = 28;

    /** The most efficiency points a GEOM block can describe, bounded by its 32-bit size field. */
    static constexpr size_t sm_max_efficiency_points = 4080;

    /** ENGCAL is a fixed four-float field at ACQP common offset 0x32E, and `GetEnergyCalibration()`
     reads back exactly four - so `AddEnergyCalibration()` writes at most this many, and any beyond
     are dropped rather than allowed to run into the fields that follow.
     */
    static constexpr size_t max_energy_cal_coefs = 4;

    // GEOM block geometry, taken from a real Genie-written block (the Ba-133 test file):
    //  commonFlag 0x0500, recOffset 32, entOffset 0x04B2, entSize 33, model name at recOffset+222.
    static constexpr uint16_t sm_geom_rec_offset = 32;
    static constexpr uint16_t sm_geom_ent_offset = 0x04B2;
    static constexpr uint16_t sm_geom_ent_size = 33;

    // PEAK block geometry, taken from Genie-written files (CNFreader's cs137.CNF, and the Ba-133
    //  test file), which agree on all of it.  The block's "common" area names the algorithms that
    //  produced the peaks; Genie writes these whenever a PEAK block is populated, and a file
    //  without them appears to have its peak records ignored.
    static constexpr size_t sm_genie_peak_block_size = 0x3000;
    static constexpr size_t sm_genie_peak_search_name_offset = 0x3C;
    static constexpr size_t sm_genie_peak_fit_name_offset = 0x9D;
    /** The two "when this ran" timestamps in that common area; one second apart in the Ba-133
     file, so presumably the search and then the fit.  These offsets are where the real files put
     them - do not derive them from the name offsets, the first name has no timestamp before it.
     */
    static constexpr size_t sm_genie_peak_time_offsets[2] = { 0x8D, 0xB9 };

    /** DISP block geometry, again from the Genie-written files, which all agree.

     The DISP block is the region-of-interest list that goes with a PEAK block: one 16-byte record
     per ROI, holding the ROI's channel range and how many peaks sit in it (so a doublet is one
     DISP record referencing two PEAK records).  Genie appears to need it to make sense of a PEAK
     block - peaks written without it are ignored.
     */
    static constexpr uint16_t sm_disp_rec_size = 0x0010;
    static constexpr uint16_t sm_disp_rec_offset = 0x019C;
    static constexpr size_t sm_genie_disp_block_size = 0x0E00;

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

    /** Returns the nuclide library lines read from the file; the result is cached, so calling
     this repeatedly is cheap and returns the same lines each time.

     Note: this used to *append* to its cache on every call, so calling it twice - including the
     easy-to-miss `GetNuclides(); GetLines();` sequence, since `GetNuclides()` populates the line
     cache as a side effect - returned every line duplicated, and made `GetNuclides()` itself
     wrong (it indexes into the line cache by line number).

     Throws if the file has no NLINES block.
     */
    std::vector<Line>& GetLines();

    /** Returns the nuclides read from the file; populates the `GetLines()` cache as a side
     effect if it is empty.

     Throws if the file has no NUCL block, or no lines.
     */
    std::vector<Nuclide>& GetNuclides();
    std::vector<Peak>& GetPeaks();
    std::vector<EfficiencyPoint>& GetEfficiencyPoints();
    /** The efficiency model named by the file; only valid once `GetEfficiencyPoints()` has been
     called - until then this returns `EfficiencyModel::NotReadin`.
     */
    EfficiencyModel GetEfficiencyModel() const;
    SpecUtils::time_point_t GetSampleTime();
    SpecUtils::time_point_t GetAquisitionTime();
    float GetLiveTime();
    float GetRealTime();
    /** The four shape-calibration coefficients: `{B0, B1, B2, B3}`, where
     `FWHM = B0 + B1*sqrt(E)` and the low tail is `T(E) = B2 + B3*E` (see `Peak::LowTail`).
     */
    std::vector<float>& GetShapeCalibration();
    std::vector<float>& GetEnergyCalibration();
    std::vector<uint32_t>& GetSpectrum();
    std::string GetSampleTitle();
    DetInfo& GetDetectorInfo();

    /** Returns the number of channels stored in the ACQP parameter section.
     This is the authoritative channel count (stored as uint32 at acqpCommon offset 0x89).
     Returns 0 if not available.
     */
    uint32_t GetNumChannelsFromAcqp();

    /** Reads additional string fields from the SAMP block.
     These fields are at fixed offsets in the SAMP data area.
     Strings that are empty or all-null in the file will be returned as empty strings.

     @param[out] sample_id        Sample ID (16 chars at offset 0x40)
     @param[out] sample_type      Sample type (16 chars at offset 0x80)
     @param[out] sample_units     Sample quantity units (16 chars at offset 0x94)
     @param[out] sample_geometry  Geometry description (16 chars at offset 0xA4)
     @param[out] user_name        Operator/user name (24 chars at offset 0x2A6)
     @param[out] sample_desc      Sample description (256 chars at offset 0x33E)
     @return true if a SAMP block was found and read
     */
    bool GetSampleStrings( std::string &sample_id,
                           std::string &sample_type,
                           std::string &sample_units,
                           std::string &sample_geometry,
                           std::string &user_name,
                           std::string &sample_desc );
    
    /** Gets K-edge densitometry (HKED) information if present in the file.
     
     Reads K-edge related parameters from the SAMP block and block 0x00012024:
     - Temperature (CAM_F_STEMP) from SAMP block offset 0x25a
     - Path length (CAM_F_KCPATHLEN) from block 0x00012024 offset 0x90
     
     @return KEdgeInfo struct with the K-edge parameters. Check hasInfo member
             to determine if valid K-edge info was found.
     */
    KEdgeInfo GetKEdgeInfo();

    /** Reads GPS data from the SAMP block if present.

     EXPERIMENTAL: The GPS offsets used here (latitude at 0x8D0, longitude at 0x928,
     speed at 0x938, position time at 0x940) match what we write in AddGPSData, but have
     NOT been validated against files produced by Canberra/Mirion Genie 2000.

     @param[out] latitude  GPS latitude in degrees
     @param[out] longitude GPS longitude in degrees
     @param[out] speed     Speed value
     @param[out] position_time  Time of the GPS fix (may be epoch/invalid if not stored)
     @return true if GPS data was found and appears valid (non-zero lat/lon)
     */
    bool GetGPSData( double &latitude, double &longitude,
                     double &speed, SpecUtils::time_point_t &position_time );

    // add data to CAMIO object for later file writing
    void AddNuclide(const std::string& name, const float halfLife, 
        const float halfLifeUnc, const std::string& halfLifeUnit, const int nucNo = -1);
    void AddNuclide(const Nuclide& nuc);
    void AddLine(const float energy, const float enUnc, const float yield, 
        const float yieldUnc, const int nucNo, const bool key = false);
    void AddLine(const Line& line);
    void AddLineAndNuclide(const float energy,  const float yield, const std::string& name, 
        const float halfLife, const std::string& halfLifeUnit, const bool noWeightMean = false,
        const float enUnc = -1, const float yieldUnc = -1, const float halfLifeUnc = -1,
        const bool isKeyLine = false );
    void AddEnergyCalibration(const std::vector<float> coefficients);
    void AddDetectorType(const std::string& detector_type);

    /** Writes a real FWHM = fwhmOffset + fwhmSlope*sqrt(energy) shape calibration.

     Writes the same three fields `AddDetectorType(...)` does, so it must be called *after* it to
     override the detector-type default rather than be overridden by it.
     */
    void AddShapeCalibration(const float fwhmOffset, const float fwhmSlope);

    /** Writes the low-tail shape calibration `T(E) = lowTailOffset + lowTailSlope*E`, where `T` is
     the distance below a peak's centroid at which the Gaussian core is stitched to an exponential
     tail - see `Peak::LowTail`.  Units follow the energy calibration, i.e. keV.

     These are the third and fourth coefficients `GetShapeCalibration()` returns; leaving them zero
     (the default) tells Genie the peaks have no low-energy tail.
     */
    void AddLowTailCalibration(const float lowTailOffset, const float lowTailSlope);

    /** Sets the efficiency model tag (e.g. "DUAL", "SPLINE") that will be written
     with the efficiency points added via `AddEfficiencyPoint(s)(...)`.
     */
    void AddEfficiencyModel(const EfficiencyModel model);

    /** Adds a single energy/efficiency/efficiency-uncertainty point to be written
     into the file's GEOM block.  See also `AddEfficiencyPoints(...)`.

     EXPERIMENTAL: this write path mirrors the field layout used by `ReadGeometryBlock()`/
     `GetEfficiencyPoints()` (which has been used against real Genie 2000 CNF files), but the
     GEOM-block write path itself has NOT been validated against real Canberra/Mirion Genie 2000
     software - only round-tripped against this same class's read implementation.
     */
    void AddEfficiencyPoint(const float energy, const float efficiency, const float efficiencyUncertainty);
    void AddEfficiencyPoints(const std::vector<EfficiencyPoint>& points);

    /** Adds a fitted peak to be written into the file's PEAK block.

     The block header and the per-record field layout were both checked against a Genie-produced
     CNF containing a real peak record (see `Peak` for the values used); every field this writes
     was confirmed to decode back to the expected quantity.  What has NOT been verified is real
     Genie *reading* a file we wrote.

     See `Peak` for the units - note in particular that `FullWidthAtHalfMaximum` is in keV, and
     `CountRateUncertainty` is a relative percent.
     */
    void AddPeak(const Peak& peak);
    void AddPeaks(const std::vector<Peak>& peaks);
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
    /** Picks a "key" line for every nuclide that does not already have one explicitly marked. */
    void AssignKeyLines();
    std::vector<uint8_t> GenerateGeometryBlock(size_t loc);
    std::vector<uint8_t> GeneratePeakBlock(size_t loc);

    /** The ROI list that accompanies `GeneratePeakBlock()`; see `sm_disp_rec_size`.  ROIs are
     taken from `writePeaks` by grouping peaks that share a channel range, so a doublet fitted in
     one region yields one DISP record saying two peaks are in it.
     */
    std::vector<uint8_t> GenerateDispBlock(size_t loc);

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


/** Additional GENIE-specific data (nuclide library, FWHM shape calibration, and/or
 efficiency curve) that `SpecUtils::SpecFile::write_cnf(...)` can write into a CNF file, in
 addition to the normal spectrum data.  See `ExportSpecFileCAM` in InterSpec for how these are
 built up from a spectrum's fitted peaks and detector response function.
 */
struct CnfGenieExtras
{
    /** One nuclide/x-ray line to write into the library, in the form accepted by
     `CAMIO::AddLineAndNuclide(...)`; lines sharing the same `nuclide_name` are automatically
     grouped into a single library nuclide entry, and Genie's key-line selection
     (`CAMIO::AssignKeyLines()`) runs automatically when the file is created - there is no
     explicit per-line "is key line" input.
     */
    struct LibraryLine
    {
        std::string nuclide_name;
        float half_life_seconds = 0.0f;
        /** Negative (the default) leaves the half-life uncertainty for `CAMIO` to estimate. */
        float half_life_uncert_seconds = -1.0f;
        float energy = 0.0f;
        /** Negative (the default) leaves the energy uncertainty for `CAMIO` to estimate. */
        float energy_uncert = -1.0f;
        /** Emission probability as a PERCENT (e.g. 85.3 for Cs137's 661.7 keV line), not a
         fraction - this is what Genie's own library files store.
         */
        float yield = 0.0f;
        /** Negative (the default) leaves the yield uncertainty for `CAMIO` to estimate. */
        float yield_uncert = -1.0f;
        /** Excludes this line from Genie's weighted-mean activity determination; set true for x-ray lines. */
        bool no_weight_mean = false;

        /** Marks this as the nuclide's "key" line - the one Genie uses to decide the nuclide is
         present.  If NO line of a given nuclide is marked, `CAMIO::AssignKeyLines()` picks one
         for that nuclide; if any line is marked, the caller's choice is used verbatim, so what a
         GUI previews and what lands in the file cannot disagree.
         */
        bool is_key_line = false;
    };

    /** Lines are in GENIE's units: `energy` in keV, `yield` as a **percent** (0-100), matching
     what `CAMIO::Line::Abundance` holds and what Genie-produced library files contain.
     */
    std::vector<LibraryLine> library_lines;

    /** {FWHMOFF, FWHMSLOPE} for `FWHM = FWHMOFF + FWHMSLOPE*sqrt(energy)`; if not set,
     `write_cnf` falls back to its normal hardcoded HPGe/NaI default shape calibration.
     */
    std::optional<std::pair<float,float>> shape_cal;

    /** `{B2, B3}` of the low-tail calibration `T(E) = B2 + B3*E`; see
     `CAMIO::AddLowTailCalibration(...)`.  Leave unset for peaks with no low-energy tail.
     */
    std::optional<std::pair<float,float>> low_tail_cal;

    std::optional<CAMIO::EfficiencyModel> eff_model;
    std::vector<EfficiencyPoint> eff_points;

    /** Fitted peaks to write into the file's PEAK block; see `CAMIO::AddPeak(...)` for the units,
     and for how much (little) the PEAK write path has been validated.
     */
    std::vector<Peak> peaks;

    /** If true, no spectrum (and hence no SPEC or SAMP block) is written - the result is a
     nuclide-library / calibration-only CAM file, of the same shape as the `.nlb` files Genie's
     Library Editor produces.

     `SpecFile::write_cnf(...)` still needs a usable measurement to take the live/real times,
     detector type and energy calibration from; it just leaves the channel counts out.
     */
    bool omit_spectrum = false;

    /** If true, the energy calibration is left out of the file (`ECALFLAGS` is set to
     "shape calibration only" and no coefficients are written).

     Only honored when `omit_spectrum` is also true: channel counts are not interpretable without
     their energy calibration, so a file that carries a spectrum always carries the calibration.
     */
    bool omit_energy_calibration = false;
};//struct CnfGenieExtras


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
