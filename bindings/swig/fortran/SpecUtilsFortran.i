%module SpecUtilsWrap

%{
#include <SpecUtils/SpecFile.h>
#include <SpecUtils/ParseUtils.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/DateTime.h>
#include <SpecUtils/StringAlgo.h>
#include <SpecUtils/Filesystem.h>
#include <SpecUtils/PcfExtensions.h>
%}


%include "stl.i"
//%include "std_vector.i"
/* instantiate the required template specializations */
namespace std {
    //%template(IntVector)    vector<int>;
    //%template(DoubleVector) vector<double>;
    %template(FloatVector)  vector<float>;
    %template(MeasurementVector)  vector<SpecUtils::Measurement>;
    
}

%include "std_shared_ptr.i"
%shared_ptr(vector<SpecUtils::Measurement>)
%shared_ptr(SpecUtils::Measurement)
%shared_ptr(SpecUtils::MeasurementExt)
%shared_ptr(SpecUtils::EnergyCalibration)
%shared_ptr(SpecUtils::EnergyCalibrationExt)
//%shared_ptr(std::vector<float>) // this casued me problems -hugh

%include "std_string.i"
%apply std::string { std::string& }
//%include "std_wstring.i"
%include "wchar.i"

//%template(StringVector)  std::vector<std::string>;

%ignore combine_gamma_channels;
%ignore truncate_gamma_channels; 
//%ignore set_energy_calibration;
%ignore descriptionText;
%ignore operator=;
%ignore set_gamma_counts;

%include <typemaps.i>

%apply int { size_t }

%apply SWIGTYPE ARRAY[ANY][ANY][ANY][ANY][ANY] { float[ANY][ANY][ANY][ANY][ANY] };

//%rename(set_ecal) SpecUtils::Measurement::set_energy_calibration;
%ignore SpecUtils::SpecFile::set_energy_calibration;

%include "SpecUtils/SpecFile.h"


%extend SpecUtils::Measurement
{
    /// Return the count at a given index.
    /// @param index is 1-based 
    float gamma_count_at(int index) 
    {
        return $self->gamma_counts()->at(index-1);
    }

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

    %apply (SWIGTYPE *DATA, size_t SIZE) { (const float* spectrum, size_t num_channels) };
    void set_spectrum(const float *spectrum, size_t num_channels)
    {
        SpecUtils::FloatVec counts;
        for (size_t i = 0; i < num_channels; i++)
        {
            counts.push_back(spectrum[i]);
        }
        
        $self->set_gamma_counts(counts); 
    }

    %apply (SWIGTYPE *DATA, size_t SIZE) { (float* spectrum, size_t num_channels) };
    void get_spectrum(float *spectrum, size_t num_channels)
    {
        auto & counts = *$self->gamma_counts();
        for (size_t i = 0 ; i < num_channels  ; i++)
        {
            spectrum[i] = counts.at(i);
        }        
    }

}

%extend SpecUtils::SpecFile
{
    /// Return the measurement at a given index.
    /// @param index is 1-based 
    std::shared_ptr<const SpecUtils::Measurement> measurement_at(int index)
    {
        return $self->measurement(static_cast<size_t>(index-1));
    }

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

//%include "SpecUtils/DateTime.h"

%include "std_pair.i"

%template(DevPair) std::pair<float, float>;
%template(DeviationPairs) std::vector<std::pair<float, float>>;

%ignore set_lower_channel_energy; 
%ignore energy_cal_from_CALp_file;

%include "SpecUtils/EnergyCalibration.h"

%extend SpecUtils::EnergyCalibration
{
    // void set_deviation_pairs(std::vector<std::pair<float, float>> devPairs)
    // {
    //     $self->m_deviation_pairs = devPairs;
    // }
}

%ignore make_canonical_path;

%include "SpecUtils/FileSystem.h"

