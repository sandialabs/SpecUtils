#include <SpecUtils/SpecFile.h>
#include <SpecUtils/ParseUtils.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/DateTime.h>
#include "../SpecUtils/PcfExtensions.h"
#include <filesystem>
namespace fs = std::filesystem;

#undef isnan // Undefine the isnan macro (compile failure in doctest.h on Windows)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <random>

void CheckFileExistanceAndDelete(fs::path filePath)
{
    // Check if the file exists
    if (fs::exists(filePath))
    {
        std::cout << "File exists. Deleting the file..." << std::endl;
        // Delete the file
        if (fs::remove(filePath))
        {
            std::cout << "File deleted successfully." << std::endl;
        }
    }
}
using SpecUtils::FloatVec;
using SpecUtils::FloatVecPtr;
using SpecUtils::time_point_t;

time_point_t getStartTime()
{
    const auto now_sys = std::chrono::system_clock::now();
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(now_sys);
    return now;
}

void getGammaSpectrum(FloatVec &spectrum)
{
    for (size_t i = 0; i < 128; i++)
    {
        spectrum.push_back(i * 1.0F);
    }
}

std::string getDetectorName(int panel, int column, int mca, bool isNeutron = false) {
    // Validate input parameters
    if (panel < 1 || column < 1 || mca < 1) {
        throw std::invalid_argument("Panel, column, and MCA numbers must be greater than 0.");
    }

    // Convert panel, column, and MCA to the appropriate characters
    char panelChar = 'A' + (panel - 1); // 'A' for panel 1, 'B' for panel 2, etc.
    char columnChar = 'a' + (column - 1); // 'a' for column 1, 'b' for column 2, etc.
    char mcaChar = '1' + (mca - 1); // '1' for MCA 1, '2' for MCA 2, etc.

    // Construct the detector name
    std::string detectorName;
    detectorName += panelChar;
    detectorName += columnChar;
    detectorName += mcaChar;

    // Append 'N' if it's a neutron detector
    if (isNeutron) {
        detectorName += 'N';
    }

    return detectorName;
}

std::array<std::string, 10> generateDetectorNames() {
    std::array<std::string, 10> detectorNames;

    // Random number generation setup
    static std::random_device rd; // Obtain a random number from hardware
    static std::mt19937 eng(rd()); // Seed the generator
    std::uniform_int_distribution<> panelDist(1, 4); // Panel numbers from 1 to 5
    std::uniform_int_distribution<> columnDist(1, 4); // Column numbers from 1 to 5
    std::uniform_int_distribution<> mcaDist(1, 8); // MCA numbers from 1 to 3
    std::uniform_int_distribution<> neutronDist(0, 1); // Randomly decide if it's a neutron detector

    // Generate 10 random detector names
    for (int i = 0; i < 10; ++i) {
        int panel = panelDist(eng);
        int column = columnDist(eng);
        int mca = mcaDist(eng);
        //bool isNeutron = neutronDist(eng) == 1; // 50% chance of being a neutron detector
        bool isNeutron = false; // 50% chance of being a neutron detector

        detectorNames[i] = getDetectorName(panel, column, mca, isNeutron);
    }

    std::sort(detectorNames.begin(), detectorNames.end());

    return detectorNames;
}


std::shared_ptr<SpecUtils::MeasurementExt> makeMeasurement(int id, std::string detName, char tag)
{
    auto m = std::make_shared<SpecUtils::MeasurementExt>();

    //auto detName = "Aa" + std::to_string(id);
    m->set_detector_name(detName); 

    m->set_pcf_tag(tag);

    m->set_start_time(getStartTime());

    auto title = "Test Measurement " + std::to_string(id) + " Det=" + detName;
    m->set_title(title);

    auto descr = "test_descr " + std::to_string(id);
    m->set_description(descr);

    auto source = "source " + std::to_string(id);
    m->set_source(source);

    SpecUtils::FloatVec ncounts{id + 99.0F};
    m->set_neutron_counts(ncounts, 0.0F);
    m->set_live_time(id + 10.55F);
    m->set_real_time(id + 11.66F);

    SpecUtils::FloatVec spectrum;
    getGammaSpectrum(spectrum);

    m->set_gamma_counts(spectrum);

    auto ecal = std::make_shared<SpecUtils::EnergyCalibration>();
    // auto coeffs = std::vector{4.41F, 3198.33F, 1.0F, 2.0F, 1.5f};
    const auto coeffs = std::vector{id * 2.0F, id * 500.0F, id * 20.0F, id * 30.0F, id * 3.0F};

    auto devPairs = SpecUtils::DeviationPairs();
    for (size_t i = 0; i < 20; i++)
    {
        auto devPair = std::make_pair(id + i + 10.0, id + i * -1.0F);
        devPairs.push_back(devPair);
    }

    ecal->set_full_range_fraction(spectrum.size(), coeffs, devPairs);
    m->set_energy_calibration(ecal);

    return m;
}

TEST_CASE("Round Trip")
{
    auto fname = std::string("round-trip-cpp.pcf");
    auto n42Fname = fname+".n42";

    SUBCASE("Write PCF File")
    {
        SpecUtils::PcfFile specfile;
        CheckFileExistanceAndDelete(fname);
        CheckFileExistanceAndDelete(n42Fname);

        //auto detNames = generateDetectorNames();
        //std::vector<std::string> detNames = { "Ba1", "Aa2", "Bc3", "Cb4" }; // Bc3 computes an out of range index
        std::vector<std::string> detNames = { "Ba1", "Aa2", "Bb3", "Cb4" };

        auto tags = std::vector<char>{'T', 'K', '-', '<'};
        auto numMeasurements = detNames.size();

        for (size_t i = 0; i < numMeasurements; i++)
        {
            auto detName = detNames[i];
            auto tag = tags[i];
            auto m = makeMeasurement(i + 1, detName, tag);
            specfile.add_measurement(m);
        }

        {
            auto &m = *(specfile.get_measurement_at(0));

            CHECK(m.panel() == 2 - 1);
            CHECK(m.column() == 1 - 1);
            CHECK(m.mca() == 1 - 1);
        }

        {
            auto &m = *(specfile.get_measurement_at(2));

            CHECK(m.panel() == 2 - 1);
            CHECK(m.column() == 2 - 1);
            CHECK(m.mca() == 3 - 1);
        }

        specfile.write_to_file(fname, SpecUtils::SaveSpectrumAsType::Pcf);
        specfile.write_to_file(n42Fname, SpecUtils::SaveSpectrumAsType::N42_2012);
        

        SUBCASE("Read PCF File")
        {
            SpecUtils::PcfFile specfileToRead;
            specfileToRead.read(fname);

            for (size_t i = 0; i < numMeasurements; i++)
            {
                auto &expectedM = *(specfile.get_measurement_at(i));
                auto &actualM = *(specfileToRead.get_measurement_at(i));
                CHECK(expectedM.title() == actualM.title());
                CHECK(actualM.pcf_tag() != '\0');
                CHECK(expectedM.pcf_tag() == actualM.pcf_tag());

                CHECK_FALSE(actualM.detector_name().empty());
                CHECK(actualM.detector_name() == expectedM.detector_name() );
                CHECK(actualM.detector_number() == expectedM.detector_number() );

                // times for PCFs should be compared as vax strings.
                auto timeStr1 = SpecUtils::to_vax_string(expectedM.start_time());
                auto timeStr2 = SpecUtils::to_vax_string(actualM.start_time());
                CHECK(timeStr1 == timeStr2);

                auto &expSpectrum = *expectedM.gamma_counts();
                auto &actualSpectrum = *expectedM.gamma_counts();
                auto sum = std::accumulate(actualSpectrum.begin(), actualSpectrum.end(), 0);
                CHECK(sum > 0);
                CHECK(expSpectrum == actualSpectrum);

                CHECK(actualM.live_time() > 0.0F);
                CHECK(actualM.live_time() == expectedM.live_time());

                CHECK(actualM.real_time() > 0.0F);
                CHECK(actualM.real_time() == expectedM.real_time());

                CHECK(actualM.neutron_counts().at(0) > 0);
                CHECK(actualM.neutron_counts() == expectedM.neutron_counts());

                CHECK_FALSE(actualM.get_description().empty());
                CHECK(actualM.get_description() == expectedM.get_description());

                CHECK_FALSE(actualM.get_source().empty());
                CHECK(actualM.get_source() == expectedM.get_source());

                auto newEcal = *actualM.energy_calibration();
                CHECK(newEcal.coefficients() == expectedM.energy_calibration()->coefficients());

                CHECK_MESSAGE(expectedM.deviation_pairs() == actualM.deviation_pairs(), "devpair assert failed at: ", actualM.detector_name());
            }
        }

        SUBCASE("Writing over existing file fails")
        {
            CHECK_THROWS(specfile.write_to_file(fname, SpecUtils::SaveSpectrumAsType::Pcf));
        }
    }
}

int generateRandomNumber(int min = 64, int max = 1024)
{
    // Create a random device and seed the random number generator
    std::random_device rd;
    std::mt19937 gen(rd());

    // Define the range
    std::uniform_int_distribution<> dis(min, max);

    // Generate and return the random number
    return dis(gen);
}

TEST_CASE("Get Max Channel Count")
{
    SpecUtils::SpecFile specfile;
    auto numMesurments = 20;
    int max_channel_count = 0; // Initialize max_channel_count

    for (size_t i = 0; i < numMesurments; i++)
    {
        auto m = std::make_shared<SpecUtils::Measurement>();
        auto numChannels = generateRandomNumber();

        // Update max_channel_count
        max_channel_count = std::max(max_channel_count, numChannels);

        FloatVec spectrum;
        for (size_t i = 0; i < numChannels; i++)
        {
            spectrum.push_back(i * 1.0F);
        }

        m->set_gamma_counts(spectrum);
        specfile.add_measurement(m);
    }

    CHECK(max_channel_count > 0);
}

TEST_CASE("Find Source String")
{
    std::vector<std::string> remarks{
        "Description: TestDescription",
        "Source: TestSource"};

    SpecUtils::MeasurementExt m;
    m.set_remarks(remarks);

    auto expected = std::string("TestSource");
    auto actual = m.get_source();

    CHECK(actual == expected);
    SUBCASE("Find Description String")
    {

        auto expected = std::string("TestDescription");
        auto actual = m.get_description();

        CHECK(actual == expected);
    }
}

TEST_CASE("No Description Yields Empty String")
{
    std::vector<std::string> remarks{
        "DescriptionZZZZZ: TestDescription",
        "SourceYYYYY: TestSource"};

    SpecUtils::MeasurementExt m;
    m.set_remarks(remarks);

    auto expected = std::string();
    auto actual = m.get_description();

    CHECK(actual == expected);

    SUBCASE("No Source Yields Empty String")
    {
        auto expected = std::string();
        auto actual = m.get_source();

        CHECK(actual == expected);
    }
}
