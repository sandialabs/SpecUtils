%module SpecUtilsWrap

%{
#include <SpecUtils/SpecFile.h>
#include <SpecUtils/ParseUtils.h>
#include <SpecUtils/EnergyCalibration.h>
#include <SpecUtils/DateTime.h>
#include <SpecUtils/StringAlgo.h>
%}


%include "stl.i"
//%include "std_vector.i"
/* instantiate the required template specializations */
namespace std {
    %template(IntVector)    vector<int>;
    %template(DoubleVector) vector<double>;
    %template(FloatVector)  vector<float>;
    %template(MeasurementVector)  vector<SpecUtils::Measurement>;
    
}

//%template(TimePoint) std::chrono::time_point<std::chrono::system_clock,std::chrono::microseconds>;


%include "std_shared_ptr.i"
%shared_ptr(vector<SpecUtils::Measurement>)
%shared_ptr(SpecUtils::Measurement)
%shared_ptr(std::vector<float>)

%include "std_string.i"
%apply std::string { std::string& }
//%include "std_wstring.i"
%include "wchar.i"

//%template(StringVector)  std::vector<std::string>;

%ignore combine_gamma_channels;
%ignore truncate_gamma_channels; 
%ignore set_energy_calibration;
%ignore descriptionText;
%ignore operator=;
%ignore set_gamma_counts;

%include <typemaps.i>


%include "SpecUtils/SpecFile.h"

%extend SpecUtils::Measurement
{
    /// Return the count at a given index.
    /// @param index is 1-based 
    float gamma_count_at(int index) 
    {
        return $self->gamma_counts()->at(index-1);
    }

    std::string get_description()
    {
        auto &remarks = $self->remarks();
        return SpecUtils::get_description(remarks);
    }

    std::string get_source()
    {
        auto &remarks = $self->remarks();
        return SpecUtils::get_source(remarks);
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

    void set_description(std::string description)
    {
        auto remarks = $self->remarks();

        // If there is already a description, remove it first.
        auto it = remarks.begin();
        for(; it != remarks.end(); ) {
            if(SpecUtils::istarts_with(*it, "Description:"))
                it = remarks.erase(it);
            it++;
        }
        remarks.push_back( "Description: " + description );
        $self->set_remarks(remarks);
    }

    void set_source(std::string source)
    {
        auto remarks = $self->remarks();

        // If there is already a source, remove it first.
        auto it = remarks.begin();
        for(; it != remarks.end(); ) {
            if(SpecUtils::istarts_with(*it, "source:"))
                it = remarks.erase(it);
            it++;
        }
        remarks.push_back( "Source: " + source );
        $self->set_remarks(remarks);
    }

    void set_neutron_count(float count)
    {
        SpecUtils::FloatVec ncounts{count};
        $self->set_neutron_counts(ncounts, 0.0F);
    }
    //%apply (SWIGTYPE ARRAY[], size_t num_channels) { (const float* spectrum, size_t num_channels) };
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
}

//%include "SpecUtils/DateTime.h"

%include "std_pair.i"

%template(DevPair) std::pair<float, float>;
//%template(DeviationPairs) std::vector<SpecUtils::DevPair>;

%ignore set_lower_channel_energy; 
%ignore energy_cal_from_CALp_file;

%include "SpecUtils/EnergyCalibration.h"