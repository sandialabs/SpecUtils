#include <SpecUtils/SpecFile.h>
#include <SpecUtils/ParseUtils.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/DateTime.h>
#include <filesystem>
namespace fs = std::filesystem;

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

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

        m->set_start_time(  now );
        m->set_title("Test Measurment");
        auto remarks = m->remarks();
        remarks.push_back("Description: test_descr");
        remarks.push_back("Source: source123");
        m->set_remarks(remarks);

        FloatVec ncounts{99.0F};
        m->set_neutron_counts(ncounts, 0.0F);

        FloatVec spectrum;
        for (size_t i = 0; i < 128; i++)
        {
            spectrum.push_back(i * 1.0F);
        }

        m->set_gamma_counts(spectrum);
        m->set_live_time(99.0F);
        m->set_real_time(100.0F);

        auto ecal = std::make_shared<SpecUtils::EnergyCalibration>();
        auto coeffs = std::vector{4.41F, 3198.33F, 0.0F, 0.0F};
        ecal->set_full_range_fraction(spectrum.size(), coeffs, {} );
        m->set_energy_calibration(ecal);

        specfile.add_measurement(m);

        specfile.write_to_file(fname, SpecUtils::SaveSpectrumAsType::Pcf);

        SUBCASE("Read PCF File")
        {
            SpecUtils::SpecFile specfileToRead;
            auto success = specfileToRead.load_file(fname, SpecUtils::ParserType::Auto);
            CHECK(success);
            auto &expectedM = *(specfile.measurements().at(0));
            auto &actualM = *(specfileToRead.measurements().at(0));
            CHECK(expectedM.title() == actualM.title());

            //auto timesEqual = expectedM.start_time() == actualM.start_time();
            auto timeStr1 = SpecUtils::to_iso_string(expectedM.start_time() );
            auto timeStr2 = SpecUtils::to_iso_string(actualM.start_time() );
            CHECK( timeStr1 == timeStr2);

            auto & expSpectrum = *expectedM.gamma_counts();
            auto & actualSpectrum = *expectedM.gamma_counts();
            CHECK(expSpectrum == actualSpectrum);

            auto &rmarks = specfileToRead.remarks();
            CHECK( SpecUtils::get_description(remarks) == "test_descr" );
            CHECK( SpecUtils::get_source(remarks) == "source123" );
            
            auto newEcal = *actualM.energy_calibration();
            CHECK(newEcal.coefficients() == ecal->coefficients());
        }
    }




}