#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>

#include <SpecUtils/SpecFile.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/StringAlgo.h>

namespace SpecUtils
{

    class MeasurementExt : public SpecUtils::Measurement
    {
    public:
        MeasurementExt() : Measurement()
        {

        }

        void set_description(const std::string &description)
        {
            auto &remarks = remarks_;

            // If there is already a description, remove it first.
            auto it = remarks.begin();
            for (; it != remarks.end();)
            {
                if (SpecUtils::istarts_with(*it, "Description:"))
                    it = remarks.erase(it);
                it++;
            }
            remarks.push_back("Description: " + description);
        }

        void set_source(const std::string &source)
        {
            auto &remarks = remarks_;

            // If there is already a source, remove it first.
            auto it = remarks.begin();
            for (; it != remarks.end();)
            {
                if (SpecUtils::istarts_with(*it, "source:"))
                    it = remarks.erase(it);
                it++;
            }
            remarks.push_back("Source: " + source);
        }

        std::string get_description() const
        {
            auto retVal = std::string();
            auto descrOffset = 12U;
            for (auto str : remarks_)
            {
                if (SpecUtils::istarts_with(str, "Description:"))
                {
                    retVal = str.substr(descrOffset);
                    trim(retVal);
                    break;
                }
            }
            return retVal;
        }

        std::string get_source() const
        {
            auto retVal = std::string();
            auto sourceStringOffset = 8U;
            for (auto str : remarks_)
            {
                if (SpecUtils::istarts_with(str, "Source:"))
                {
                    retVal = str.substr(sourceStringOffset);
                    trim(retVal);
                    break;
                }
            }
            return retVal;
        }
    
        /// @brief return zero-based panel number based on detector name
        int panel() 
        {
            if (panel_ < 0)
                update_detector_name_params();
            return panel_;
        }

        /// @brief return zero-based column number based on detector name
        int column() 
        {
            if (column_ < 0)
                update_detector_name_params();
            return column_;
        }

        /// @brief return zero-based mca number based on detector name
        int mca() 
        {
            if (mca_ < 1)
                update_detector_name_params();
            return mca_;
        }

        /// @brief Overload for testing
        void set_ecal( const std::shared_ptr<const EnergyCalibration> &cal )
        {
            energy_calibration_ = cal;
        }

        protected:
        int panel_ = -1;
        int column_ = -1;
        int mca_ = -1;

        void update_detector_name_params()
        {
            auto detName = detector_name();
            pcf_det_name_to_dev_pair_index(detName, column_, panel_, mca_);
        }
    };

    class EnergyCalibrationExt : public SpecUtils::EnergyCalibration
    {
    public:
        EnergyCalibrationExt() : SpecUtils::EnergyCalibration()
        {
            m_type = EnergyCalType::FullRangeFraction;
        }

        SpecUtils::DeviationPairs &get_dev_pairs()
        {
            return m_deviation_pairs;
        }

        void set_dev_pairs(SpecUtils::DeviationPairs & devPairs)
        {
            m_deviation_pairs = devPairs;
        }

        FloatVec & get_coeffs()
        {
            return m_coefficients;
        }

    };

    class PcfFile : public SpecUtils::SpecFile
    {
    public:
        PcfFile() : SpecFile()
        {

        }
        void read(const std::string &fname)
        {
            auto success = load_pcf_file(fname);
            if (!success)
            {
                throw std::runtime_error("I couldn't open \"" + fname + "\"");
            }
        }
        void add_measurement_ext(std::shared_ptr<MeasurementExt> m)
        {
            this->add_measurement(m);
        }

        std::shared_ptr<MeasurementExt> get_measurement_at(int index)
        {
            auto m = measurements_.at(index);
            return std::dynamic_pointer_cast<MeasurementExt>(m);
        }

        virtual std::shared_ptr<Measurement> make_measurement()
        {
            return std::make_shared<MeasurementExt>();
        }
    };

    template <class StringVec>
    inline void set_panel(int panel, StringVec &remarks)
    {
        // If there is already a description, remove it first.
        auto it = remarks.begin();
        for (; it != remarks.end();)
        {
            if (SpecUtils::istarts_with(*it, "panel:"))
                it = remarks.erase(it);
            it++;
        }
        std::ostringstream sstrm;
        sstrm << "panel: " << panel;
        remarks.push_back(sstrm.str());
    }

    template <class StringVec>
    inline void set_column(int column, StringVec &remarks)
    {
        // If there is already a description, remove it first.
        auto it = remarks.begin();
        for (; it != remarks.end();)
        {
            if (SpecUtils::istarts_with(*it, "column:"))
                it = remarks.erase(it);
            it++;
        }
        std::ostringstream sstrm;
        sstrm << "column: " << column;
        remarks.push_back(sstrm.str());
    }

    template <class StringVec>
    inline int get_panel(const StringVec &vec)
    {
        auto retVal = std::string();
        auto offset = 6U;
        for (auto str : vec)
        {
            if (SpecUtils::istarts_with(str, "panel:"))
            {
                retVal = str.substr(offset);
                trim(retVal);
                break;
            }
        }
        return std::stoi(retVal);
    }

} // namespace SpecUtils