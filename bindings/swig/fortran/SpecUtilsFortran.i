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
//%include "std_vector.i"

namespace std {
    %template(FloatVector)  vector<float>;
    %template(MeasurementVector)  vector<SpecUtils::Measurement>;
}

%include "std_shared_ptr.i"
%shared_ptr(vector<SpecUtils::Measurement>)
%shared_ptr(SpecUtils::Measurement)
%shared_ptr(SpecUtils::MeasurementExt)
%shared_ptr(SpecUtils::EnergyCalibration)
%shared_ptr(SpecUtils::EnergyCalibrationExt)
//%shared_ptr(const std::vector<float>) // this caused me problems -hugh

%include "std_string.i"
%apply std::string { std::string& }
%include "wchar.i"

%ignore combine_gamma_channels;
%ignore truncate_gamma_channels; 
%ignore descriptionText;
%ignore operator=;
%ignore set_gamma_counts;

%include <typemaps.i>

%apply int { size_t }

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

    size_t get_num_channel_energies()
    {
        auto ecal = $self->energy_calibration();
        auto channel_energies = ecal->channel_energies();
        return channel_energies->size();
    }

    %apply (SWIGTYPE *DATA, size_t SIZE) { (float* energies, size_t num_bounds) };
    void get_channel_energies(float* energies, size_t num_bounds)
    {
        auto ecal = $self->energy_calibration();
        auto channel_energies = ecal->channel_energies();
        auto count = channel_energies->size();
        for (size_t i = 0; i < count; i++)
        {
            auto e = channel_energies->at(i);
            energies[i] = e;
        }
    }

}

%extend SpecUtils::SpecFile
{
    /// Return the measurement at a given index.
    /// @param index is 1-based 
    std::shared_ptr<const SpecUtils::Measurement> measurement_at(int index)
    {
        auto newIndex = static_cast<size_t>(index-1);
        return $self->measurement(newIndex);
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

%include "std_pair.i"

%template(DevPair) std::pair<float, float>;
%template(DeviationPairs) std::vector<std::pair<float, float>>;

%ignore set_lower_channel_energy; 
%ignore energy_cal_from_CALp_file;

%include "SpecUtils/EnergyCalibration.h"

%ignore make_canonical_path;

%include "SpecUtils/FileSystem.h"

