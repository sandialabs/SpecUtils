%module SpecUtilsWrap

%{
#include <SpecUtils/SpecFile.h>

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


%include "std_shared_ptr.i"
%shared_ptr(vector<SpecUtils::Measurement>)
%shared_ptr(SpecUtils::Measurement)
%shared_ptr(std::vector<float>)

%include "std_string.i"
%apply std::string { std::string& }
//%include "std_wstring.i"
%include "wchar.i"

%ignore combine_gamma_channels;
%ignore truncate_gamma_channels; 
%ignore set_energy_calibration;
%ignore descriptionText;
%ignore operator=;

%include "SpecUtils/SpecFile.h"

%extend SpecUtils::Measurement
{
    float gamma_count_at(int index) 
    {
        return $self->gamma_counts()->at(index);
    }
}

%extend SpecUtils::SpecFile
{
    std::shared_ptr<const SpecUtils::Measurement> measurement_at(int num)
    {
        return $self->measurement(static_cast<size_t>(num));
    }
}
