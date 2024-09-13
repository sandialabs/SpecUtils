#include <SpecUtils/SpecFile.h>
#include <SpecUtils/ParseUtils.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/DateTime.h>
#include <filesystem>
namespace fs = std::filesystem;

#undef isnan  // Undefine the isnan macro (compile failure in doctest.h on Windows)

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

#if 0
TEST_CASE("Time")
{
    auto timepoint = SpecUtils::time_from_string("2024-Feb-28 00:00:00.62Z");

    auto expectedTS = 1709103600.0F;

    auto tmp =  timepoint.time_since_epoch().count();
    tmp /= 1e6;
    
    CHECK( expectedTS == doctest::Approx(tmp));

    //auto tp2 = std::chrono::time_point_cast<std::chrono::seconds>( expectedTS );  
}
#endif

TEST_CASE("More Time")
{
        // Unix timestamp (seconds since epoch)
    std::time_t unix_timestamp = 1709103600; // Example timestamp

    // Convert the Unix timestamp to a time_point with second precision
    auto tp_seconds = std::chrono::system_clock::from_time_t(unix_timestamp);

    // Cast the time_point to microseconds precision
    time_point_t tp_microseconds = std::chrono::time_point_cast<std::chrono::microseconds>(tp_seconds);

    auto timestr = SpecUtils::to_vax_string(tp_microseconds); 
    // Output the time_point
    //std::time_t tp_microseconds_time_t = std::chrono::system_clock::to_time_t(tp_microseconds);
    std::cout << "Time point (microseconds precision): " << timestr << std::endl;
}

TEST_CASE("Round Trip")
{
    auto fname = std::string("round-trip-cpp.pcf");

    SUBCASE("Write PCF File")
    {
        SpecUtils::SpecFile specfile;
        CheckFileExistanceAndDelete(fname);
        auto m = std::make_shared<SpecUtils::Measurement>();

        const auto now_sys = std::chrono::system_clock::now();  
        const auto now = std::chrono::time_point_cast<std::chrono::microseconds>( now_sys );  
        std::cerr << "now: " << now.time_since_epoch().count() << std::endl;

        m->set_start_time(  now );
        m->set_title("Test Measurment");
        auto remarks = m->remarks();
        remarks.push_back("Description: test_descr");
        remarks.push_back("Source: source123");
        m->set_remarks(remarks);

        FloatVec ncounts{99.0F};
        m->set_neutron_counts(ncounts, 0.0F);
        m->set_live_time(10.55F);
        m->set_real_time(11.66F);

        FloatVec spectrum;
        for (size_t i = 0; i < 128; i++)
        {
            spectrum.push_back(i * 1.0F);
        }

        m->set_gamma_counts(spectrum);

        auto ecal = std::make_shared<SpecUtils::EnergyCalibration>();
        auto coeffs = std::vector{4.41F, 3198.33F, 1.0F, 2.0F, 1.5f};

        SpecUtils::DeviationPairs devPairs;
        //auto 
        for (size_t i = 0; i < 4; i++)
        {
            auto devPair = std::make_pair(i+10.0, i * -1.0F);
            devPairs.push_back(devPair);
        }
        
        ecal->set_full_range_fraction(spectrum.size(), coeffs, devPairs );
        //ecal->set_default_polynomial(spectrum.size(), coeffs, devPairs );
        m->set_energy_calibration(ecal);

        specfile.add_measurement(m);

        specfile.write_to_file(fname, SpecUtils::SaveSpectrumAsType::Pcf);

        SUBCASE("Read PCF File")
        {
            SpecUtils::SpecFile specfileToRead;
            auto success = specfileToRead.load_file(fname, SpecUtils::ParserType::Auto);
            CHECK(success);

            //CHECK( specfileToRead.max_channel_count() == 128 );
            auto &expectedM = *(specfile.measurements().at(0));
            auto &actualM = *(specfileToRead.measurements().at(0));
            CHECK(expectedM.title() == actualM.title());

            // times for PCFs should be compared as vax strings.
            auto timeStr1 = SpecUtils::to_vax_string(expectedM.start_time() );
            auto timeStr2 = SpecUtils::to_vax_string(actualM.start_time() );
            CHECK( timeStr1 == timeStr2);

            auto & expSpectrum = *expectedM.gamma_counts();
            auto & actualSpectrum = *expectedM.gamma_counts();
            CHECK(actualSpectrum.at(100) > 0);
            CHECK(expSpectrum == actualSpectrum);


            CHECK(actualM.live_time() > 0.0F);
            CHECK(actualM.live_time() == expectedM.live_time());

            CHECK(actualM.real_time() > 0.0F);
            CHECK(actualM.real_time() == expectedM.real_time());

            CHECK( actualM.neutron_counts().at(0) > 0 );
            CHECK( actualM.neutron_counts() == expectedM.neutron_counts() );

            auto &rmarks = specfileToRead.remarks();
            CHECK( SpecUtils::get_description(remarks) == "test_descr" );
            CHECK( SpecUtils::get_source(remarks) == "source123" );
            
            auto newEcal = *actualM.energy_calibration();
            CHECK(newEcal.coefficients() == ecal->coefficients());

            CHECK(expectedM.deviation_pairs() ==  actualM.deviation_pairs());
        }

        SUBCASE("Writing over existing file fails")
        {
            CHECK_THROWS( specfile.write_to_file(fname, SpecUtils::SaveSpectrumAsType::Pcf) );
        }

    }


}


int generateRandomNumber(int min=64, int max=1024) {
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
    
    CHECK(max_channel_count > 0 );
}

class MyEcal : public SpecUtils::EnergyCalibration
{
    public:
    SpecUtils::DeviationPairs & getDevPairs()
    {
        return m_deviation_pairs;
    }
};

TEST_CASE("Deviation Pair Map")
{
    SpecUtils::SpecFile specfile;

    auto maxDevPairs = 20;
    auto maxMCA = 8;
    auto maxPanel = 8;
    auto maxCol = 4;

    auto pairVal = 1;
    for (size_t i = 0; i < maxCol; i++)
    {
        for (size_t j = 0; j < maxPanel; j++)
        {
            for (size_t k = 0; k < maxMCA; k++)
            {
                auto m = std::make_shared<SpecUtils::Measurement>();
                MyEcal ecal;
                auto &devPairs = ecal.getDevPairs();
                for (size_t p = 0; p < maxDevPairs; p++)
                {
                    auto first = pairVal++;
                    auto second = pairVal++;

                    auto devPair = std::make_pair(first, second);
                    devPairs.push_back(devPair);
                }
            }
        }
    }
}


std::pair<float, float> getDeviationPair(size_t i, size_t j, size_t k, size_t l)
{
    const auto maxDevPairs = 20;
    const auto maxMCA = 8;
    const auto maxPanel = 8;
    const auto maxCol = 4;

    // Validate the indices to ensure they are within bounds
    if (i >= maxCol || j >= maxPanel || k >= maxMCA || l >= maxDevPairs)
    {
        throw std::out_of_range("Index out of range");
    }

    // Calculate the total number of pairs before the given indices
    size_t totalPairs = l + k * maxDevPairs + j * maxDevPairs * maxMCA + i * maxDevPairs * maxMCA * maxPanel;

    // Calculate the pairVal
    size_t pairVal = 1 + 2 * totalPairs;

    // Calculate the first and second pair values
    float first = (pairVal) * 1.0F;
    float second = (pairVal + 1) * 1.0F;

    return {first, second};
}

TEST_CASE("Deviation Pair Map Array")
{
    const auto maxDevPairs = 20;
    const auto maxMCA = 8;
    const auto maxPanel = 8;
    const auto maxCol = 4;

    auto pairVal = 0;


    // real, dimension(2,MAX_DEVIATION_PAIRS,MAX_MCA_COUNT,MAX_PANEL_COUNT,MAX_COLUMN_COUNT) :: DeviationPairs
    float deviationPairsArray[maxCol][maxPanel][maxMCA][maxDevPairs][2]; //
    for (size_t i = 0; i < maxCol; i++)
    {
        for (size_t j = 0; j < maxPanel; j++)
        {
            for (size_t k = 0; k < maxMCA; k++)
            {
                for (size_t l = 0; l < maxDevPairs; l++)
                {
                    auto first = ++pairVal * 1.0F;
                    auto second = ++pairVal * 1.0F;

                    deviationPairsArray[i][j][k][l][0] = first;
                    deviationPairsArray[i][j][k][l][1] = second;
                }
            }
        }
    }

    {
            size_t i = 0, j = 0, k = 0, l = 0;
            CHECK( deviationPairsArray[i][j][k][l][0] == 1.0F);
            CHECK( deviationPairsArray[i][j][k][l][1] == 2.0F);
    }
    {
            size_t i = 0, j = 0, k = 0, l = 9;
            CHECK( deviationPairsArray[i][j][k][l][0] == 19.0F);
            CHECK( deviationPairsArray[i][j][k][l][1] == 20.0F);
    }
    {
            size_t i = 1, j = 2, k = 3, l = 4;
            auto pair = getDeviationPair(i, j, k, l);
            CHECK( deviationPairsArray[i][j][k][l][0] == pair.first);
            CHECK( deviationPairsArray[i][j][k][l][1] == pair.second);
    }

}
