%module SpecUtilsSwig


%rename(operatorEqual) operator=;


%include "std_string.i"
%include "std_wstring.i"
%include "wchar.i"

%include "std_vector.i"
namespace std {
   %template(StringVector) vector<std::string>;
   %template(IntVector) vector<int>;
}


%include "std_shared_ptr.i"
%shared_ptr(SpecUtils::Measurement)
%shared_ptr(std::vector<float>)

namespace std {
   %template(FloatVector) vector<float>;
}




%include "cpointer.i"
%include "boost_ptime.i"







%{ 
#include <iostream> 
#include <sstream> 
#include <fstream>
#include <memory>
using namespace std; 
%} 
%inline %{ 
std::ostream* openFile(const char* filename) { 
  ofstream *filePtr = new ofstream(filename);
  return(filePtr);
} 
void closeFile(std::ostream *stream) { 
  stream->flush();
  delete(stream);
}
%} 





%{
#include <set>
#include <vector>
%}




%{
#include <string>
#include "SpecUtils/SpecFile.h"
%}



%include "SpecUtils/SpecFile.h"
