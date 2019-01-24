%{
#include <boost/date_time/posix_time/posix_time_types.hpp>
%}

%include <exception.i>

%pragma(java) jniclassimports="import org.joda.time.DateTime;" 
%pragma(java) moduleimports="import org.joda.time.DateTime;" 

%typemap(jstype) boost::posix_time::ptime "org.joda.time.DateTime"
%typemap(jtype) boost::posix_time::ptime "long"
%typemap(jni) boost::posix_time::ptime "jlong"

%typemap(out) boost::posix_time::ptime {
    if ($1.is_not_a_date_time() || $1.is_infinity()) {
      SWIG_JavaException(jenv, SWIG_ValueError, "Not a finite date-time"); return $null;
    }
    boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
    $result = ($1 - epoch).total_milliseconds();
}
%typemap(javaout) boost::posix_time::ptime {
    return new org.joda.time.DateTime($jnicall);
}

%typemap(javain) boost::posix_time::ptime "$javainput.getMillis()"
%typemap(in) boost::posix_time::ptime {
    boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
    $1 = epoch + boost::posix_time::milliseconds($input);
}

%typemap(directorin
    , descriptor="Lorg/joda/time/DateTime;"
) boost::posix_time::ptime %{
    if ($1.is_not_a_date_time() || $1.is_infinity()) {
      SWIG_JavaException(jenv, SWIG_ValueError, "Not a finite date-time"); return $null;
    }
    boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
    $input = ($1 - epoch).total_milliseconds();
%}
%typemap(javadirectorin) boost::posix_time::ptime "new DateTime($jniinput)"