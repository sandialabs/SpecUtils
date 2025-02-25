%module SpecUtilsWrap

%{
#include <SpecUtils/SpecFile.h>
#include <SpecUtils/ParseUtils.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/DateTime.h>
#include <SpecUtils/StringAlgo.h>
#include <SpecUtils/Filesystem.h>
%}


%include "stl.i"
// //%include "std_vector.i"

// namespace std {
//     %template(FloatVector)  vector<float>;
//     %template(MeasurementVector)  vector<SpecUtils::Measurement>;
    
// }

%include "std_shared_ptr.i"
%shared_ptr(vector<SpecUtils::Measurement>)
%shared_ptr(SpecUtils::Measurement)
%shared_ptr(SpecUtils::MeasurementExt)
%shared_ptr(SpecUtils::EnergyCalibration)
%shared_ptr(SpecUtils::EnergyCalibrationExt)
//%shared_ptr(std::vector<float>) // this caused me problems -hugh

%include "std_string.i"
%apply std::string { std::string& }
%include "wchar.i"

%ignore combine_gamma_channels;
%ignore truncate_gamma_channels; 
%ignore descriptionText;
%ignore operator=;
%ignore set_gamma_counts;

%include <typemaps.i>

%ignore SpecUtils::SpecFile::set_energy_calibration;

%include "SpecUtils/SpecFile.h"

%extend SpecUtils::Measurement
{

    size_t get_num_channels()
    {
        return $self->gamma_counts()->size();
    }

    std::string get_start_time_string()
    {
        auto timeStr = SpecUtils::to_vax_string( $self->start_time() );
        return timeStr;
    }

    void set_start_time_from_string(std::string time_str)
    {
        auto tp = SpecUtils::time_from_string(time_str);
        $self->set_start_time(tp);
    }
    
    void set_neutron_count(float count)
    {
        SpecUtils::FloatVec ncounts{count};
        $self->set_neutron_counts(ncounts, 0.0F);
    }

    float get_neutron_count()
    {
        auto count = 0.0F;
        if (!$self->neutron_counts().empty())
            count = $self->neutron_counts().front();

        return count;            
    }

}

%extend SpecUtils::SpecFile
{

    int get_max_channel_count()
    {
        auto maxCount = 0;
        auto numMeasurements = $self->num_measurements();

        for(int i = 0; i < numMeasurements; i++)
        {
            auto m = $self->measurement(i);
            auto numChannels = static_cast<int>(m->num_gamma_channels());
            maxCount = std::max(maxCount, numChannels);
        }

        return maxCount;            
    }
}

// %include "std_pair.i"

// %template(DevPair) std::pair<float, float>;
// %template(DeviationPairs) std::vector<std::pair<float, float>>;

%ignore set_lower_channel_energy; 
%ignore energy_cal_from_CALp_file;

%include "SpecUtils/EnergyCalibration.h"


