/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// For statically linking on Windows, we need this following define
#define BOOST_PYTHON_STATIC_LIB

// TODO: It would be nice to have the compiled results compatible with
//       any python >= 3.5, which would require this next line, but this
//       would require significant changes to use this limited API
// #define Py_LIMITED_API 0x03050000

#include "SpecUtils_config.h"

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/EnergyCalibration.h"


#if( SpecUtils_ENABLE_D3_CHART )
#include "SpecUtils/D3SpectrumExport.h"
static_assert( SpecUtils_D3_SUPPORT_FILE_STATIC,
               "For python support you should enable static D3 resources (although this isnt"
               " strictly necessary... you can comment out this static_assert at your own risk)" );
#endif //SpecUtils_ENABLE_D3_CHART


#include <set>
#include <iosfwd>
#include <vector>
#include <string>
#include <locale>
#include <limits>
#include <memory>
#include <iostream>
#include <iostream>
#include <stdexcept>
#include <algorithm>

#include <boost/python.hpp>
#include <boost/python/args.hpp>
#include <boost/python/list.hpp>
#include <boost/python/exception_translator.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>

#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/concepts.hpp>


#include <datetime.h>  // compile with -I/path/to/python/include


using namespace std;


namespace
{
  //Begin structs to help convert between c++ and python
  
  class PythonInputDevice
  {
  public:
    typedef char char_type;
   
    // XXX - not working when tellg needs to be called
    
    //  struct category : boost::iostreams::source_tag, boost::iostreams::seekable_device_tag
    //  {};
    //  struct category : boost::iostreams::source_tag {};
    typedef boost::iostreams::seekable_device_tag category;
    
    explicit PythonInputDevice( boost::python::object object ) : object_(object)
    {}
    
    std::streamsize write( const char *s, std::streamsize n )
    {
      return 0;
    }
    
    std::streamsize read( char_type *buffer, std::streamsize buffer_size )
    {
       boost::python::object pyread = object_.attr( "read" );
      if( pyread.is_none() )
        throw std::runtime_error( "Python stream has no attibute 'read'" );
      
      boost::python::object py_data = pyread( buffer_size );
      const std::string data = boost::python::extract<std::string>( py_data );
      
      if( data.empty() && buffer_size != 0 )
        return -1;
      
      std::copy( data.begin(), data.end(), buffer );
      
      return data.size();
    }//read()
    
    
    boost::iostreams::stream_offset seek( boost::iostreams::stream_offset offset,
                                         std::ios_base::seekdir way )
    {
      int pway = 0;
      switch( way )
      {
        case std::ios_base::beg: pway = 0; break;
        case std::ios_base::cur: pway = 1; break;
        case std::ios_base::end: pway = 2; break;
      }//switch( way )
      
      boost::python::object pyseek = object_.attr( "seek" );
      boost::python::object pytell = object_.attr( "tell" );
      
      if( pyseek.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'seek'" );
      
      if( pytell.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'tell'" );
      
      const boost::python::object pynewpos = pyseek( offset, pway );
      const boost::python::object pyoffset = pytell();
      const boost::python::extract<std::streamoff> newpos( pyoffset );
    
      return newpos;
    }//seek()
    
  private:
    boost::python::object object_;
  };//class PythonInputDevice
  
  
  class PythonOutputDevice
  {
    //see http://stackoverflow.com/questions/26033781/converting-python-io-object-to-stdostream-when-using-boostpython
  public:
    typedef char char_type;
    
    struct category : boost::iostreams::sink_tag, boost::iostreams::flushable_tag
    {};
    
    explicit PythonOutputDevice( boost::python::object object ) : object_( object )
    {}
    
    std::streamsize write( const char *buffer, std::streamsize buffer_size )
    {
      boost::python::object pywrite = object_.attr( "write" );
      
      if( pywrite.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'write'" );
      
      // If we are writing a large output (buffer_size >= 4096), we wont necassarily write
      //  all the bytes in one go.  When this happens, it seems future writes fail, and we dont
      //  get the output we want.  So instead, we will do a loop here to force writing everything...
      std::streamsize nwritten = 0;
      while( nwritten < buffer_size )
      {
        const auto buff_start = buffer + nwritten;
        const auto nbytes_to_write = buffer_size - nwritten;

        //Fine for Python 2 - just use a string
        //boost::python::str data(buff_start, nbytes_to_write); 

        // Python 3
        Py_buffer buffer;
        int res = PyBuffer_FillInfo(&buffer, 0, (void *)buff_start, nbytes_to_write, true, PyBUF_CONTIG_RO);
        if (res == -1) {
          PyErr_Print();
          throw std::runtime_error( "Python stream error writing to buffer" );
        }
        boost::python::object data( boost::python::handle<>(PyMemoryView_FromBuffer(&buffer)) );

        // Python >3.2, avoids a copy of data, but requires casting away a const
        //boost::python::object data(boost::python::handle<>(PyMemoryView_FromMemory((char *)buff_start, nbytes_to_write, PyBUF_READ)));

        boost::python::object pynwrote = pywrite( data );
        const boost::python::extract<std::streamsize> bytes_written( pynwrote );
        
        nwritten += bytes_written.check() ? bytes_written : nbytes_to_write;
      }

      return nwritten;
    }
    
    bool flush()
    {
      boost::python::object flush = object_.attr( "flush" );
      
      if( flush.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'flush'" );
        
      flush();
      return true;
    }
    
  private:
    boost::python::object object_;
  };//class PythonOutputDevice
  
  
  //wcjohns adapted the datetime convention code 20150515 from
  //  http://en.sharejs.com/python/13125
  //There is also a time duration conversion code as wel, but removed it.
  /**
   * Convert boost::posix_ptime objects (ptime and time_duration)
   * to/from python datetime objects (datetime and timedelta).
   *
   * Credits:
   * http://libtorrent.svn.sourceforge.net/viewvc/libtorrent/trunk/bindings/python/src/datetime.cpp
   * http://www.nabble.com/boost::posix_time::ptime-conversion-td16857866.html
   */
  
  
  /* Convert std::chrono::time_point to/from python */
  struct chrono_time_point_to_python_datetime
  {
    static PyObject* convert(SpecUtils::time_point_t const &t)
    {
      const chrono::time_point<chrono::system_clock,date::days> t_as_days = date::floor<date::days>(t);
      const date::year_month_day t_ymd = date::year_month_day{t_as_days};
      const date::hh_mm_ss<SpecUtils::time_point_t::duration> time_of_day = date::make_time(t - t_as_days);
    
      const int year = static_cast<int>( t_ymd.year() );
      const int month = static_cast<int>( static_cast<unsigned>( t_ymd.month() ) );
      const int day = static_cast<int>( static_cast<unsigned>( t_ymd.day() ) );
      const int hour = static_cast<int>( time_of_day.hours().count() );
      const int mins = static_cast<int>( time_of_day.minutes().count() );
      const int secs = static_cast<int>( time_of_day.seconds().count() );
      const auto microsecs = date::round<chrono::microseconds>( time_of_day.subseconds() );

      return PyDateTime_FromDateAndTime( year, month, day, hour, mins, secs, microsecs.count() );
    }
  };//struct chrono_time_point_to_python_datetime
  
  
  struct chrono_time_point_from_python_datetime
  {
    chrono_time_point_from_python_datetime()
    {
      boost::python::converter::registry::push_back( &convertible, &construct, boost::python::type_id<SpecUtils::time_point_t>() );
    }
    
    static void* convertible(PyObject * obj_ptr)
    {
      if ( ! PyDateTime_Check(obj_ptr))
        return 0;
      return obj_ptr;
    }
    
    static void construct(
                          PyObject* obj_ptr,
                          boost::python::converter::rvalue_from_python_stage1_data * data)
    {
      PyDateTime_DateTime const* pydate
      = reinterpret_cast<PyDateTime_DateTime*>(obj_ptr);
      
      // Create date object
      const short year = PyDateTime_GET_YEAR(pydate);
      const unsigned month = PyDateTime_GET_MONTH(pydate);
      const unsigned day = PyDateTime_GET_DAY(pydate);

      const int hour = PyDateTime_DATE_GET_HOUR(pydate);
      const int minute = PyDateTime_DATE_GET_MINUTE(pydate);
      const int second = PyDateTime_DATE_GET_SECOND(pydate);
      const int64_t nmicro = PyDateTime_DATE_GET_MICROSECOND(pydate);
      
      date::year_month_day ymd{ date::year(year), date::month(month), date::day(day) };
      date::sys_days days = ymd;
      SpecUtils::time_point_t tp = days;
      tp += chrono::seconds(second + 60*minute + 3600*hour);
      tp += chrono::microseconds(nmicro);
      
      // Create posix time object
      void* storage = (
                       (boost::python::converter::rvalue_from_python_storage<SpecUtils::time_point_t>*)
                       data)->storage.bytes;
      new (storage)
      SpecUtils::time_point_t(tp);
      data->convertible = storage;
    }
  };
  
  
  void pyListToSampleNumsOrNames( const boost::python::list &dn_list, 
                                std::vector<std::string> &det_names,
                                std::set<int> &det_nums )
  {
    det_names.clear();
    det_nums.clear();

    boost::python::ssize_t n = boost::python::len( dn_list );
    for( boost::python::ssize_t i = 0; i < n; ++i )
    {
      boost::python::extract<int> as_int( dn_list[i] );
      if( as_int.check() )
      {
        det_nums.insert( as_int );
      }else
      {
        boost::python::extract<std::string> as_str( dn_list[i] );
        if( !as_str.check() )
          throw std::runtime_error( "'DetectorNamesOrNumbers' must be a list of either detector numbers, or detector names." );
        det_names.push_back( as_str );
      }
    }

    if( det_names.size() && det_nums.size() )
      throw std::runtime_error( "'DetectorNamesOrNumbers' list can not mix detector numbers and detector names." );
  }//pyListToSampleNumsOrNames
  
  
  //Begin wrapper functions
  void loadFile( SpecUtils::SpecFile *info,
                 const std::string &filename,
                 SpecUtils::ParserType parser_type,
                 std::string file_ending_hint = "" )
  {
    const bool success = info->load_file( filename, parser_type, file_ending_hint );
    if( !success )
    {
      if( parser_type == SpecUtils::ParserType::Auto )
        throw std::runtime_error( "Couldnt parse file " + filename );
      
      string type;
      switch( parser_type )
      {
        case SpecUtils::ParserType::N42_2006: type = "N42-2006"; break;
        case SpecUtils::ParserType::N42_2012: type = "N42-2012"; break;
        case SpecUtils::ParserType::Spc: type = "SPC"; break;
        case SpecUtils::ParserType::Exploranium: type = "Exploranium"; break;
        case SpecUtils::ParserType::Pcf: type = "PCF"; break;
        case SpecUtils::ParserType::Chn: type = "CHN"; break;
        case SpecUtils::ParserType::SpeIaea: type = "IAEA"; break;
        case SpecUtils::ParserType::TxtOrCsv: type = "TXT or CSV"; break;
        case SpecUtils::ParserType::Cnf: type = "CNF"; break;
        case SpecUtils::ParserType::TracsMps: type = "MPS"; break;
        case SpecUtils::ParserType::SPMDailyFile: type = "SpectroscopicPortalMonitor"; break;
        case SpecUtils::ParserType::AmptekMca: type = "Amptek MCA"; break;
        case SpecUtils::ParserType::MicroRaider: type = "Micro Raider"; break;
        case SpecUtils::ParserType::RadiaCode: type = "RadiaCode"; break;
        case SpecUtils::ParserType::Aram: type = "Aram"; break;
        case SpecUtils::ParserType::OrtecListMode: type = "Ortec Listmode"; break;
        case SpecUtils::ParserType::LsrmSpe: type = "LSRM"; break;
        case SpecUtils::ParserType::Tka: type = "TKA"; break;
        case SpecUtils::ParserType::MultiAct: type = "MultiAct"; break;
        case SpecUtils::ParserType::Lzs: type = "LZS"; break;
        case SpecUtils::ParserType::Phd: type = "PHD"; break;
        case SpecUtils::ParserType::ScanDataXml: type = "ScanData"; break;
        case SpecUtils::ParserType::Json: type = "JSON"; break;
        case SpecUtils::ParserType::CaenHexagonGXml: type = "GXml"; break;
        case SpecUtils::ParserType::Auto: type = ""; break;
      }//switch( parser_type )
      
      throw std::runtime_error( filename + " couldnt be parsed as a " + type + " file." );
    }//if( !success )
  }//loadFile(...)
  
  
  //I couldnt quite figure out how to get Python to play nicely with const
  //  references to smart pointers, so instead am using some thin wrapper
  //  functions to return a smart pointer by value.
  boost::python::list channel_energies_wrapper( const SpecUtils::Measurement *meas )
  {
    boost::python::list l;
    if( !meas->channel_energies() )
     return l;
    
    for( auto p : *meas->channel_energies() )
      l.append( p );
    return l;
  }
  
  boost::python::list gamma_counts_wrapper( const SpecUtils::Measurement *meas )
  {
    boost::python::list l;
    if( !meas->gamma_counts() )
     return l;
      
    for( auto p : *meas->gamma_counts() )
      l.append( p );
    return l;
  }

  std::shared_ptr<SpecUtils::Measurement> makeCopy_wrapper( const SpecUtils::Measurement *meas )
  {
    auto m = std::make_shared<SpecUtils::Measurement>();
    *m = *meas;
    return m;
  }
  

  std::shared_ptr<SpecUtils::Measurement> makeMeasurement_wrapper()
  {
    return std::make_shared<SpecUtils::Measurement>();
  }


  void setGammaCounts_wrapper( SpecUtils::Measurement *meas,
                              boost::python::list py_counts,
                              const float live_time,
                              const float real_time )
  {
    auto counts = std::make_shared<vector<float>>();
    boost::python::ssize_t n = boost::python::len( py_counts );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      counts->push_back( boost::python::extract<float>( py_counts[i] ) );

    meas->set_gamma_counts( counts, live_time, real_time );
  }

  void setNeutronCounts_wrapper( SpecUtils::Measurement *meas,
                              boost::python::list py_counts,
                                const float live_time )
  {
    vector<float> counts;
    boost::python::ssize_t n = boost::python::len( py_counts );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      counts.push_back( boost::python::extract<float>( py_counts[i] ) );

    meas->set_neutron_counts( counts, live_time );
  }



  SpecUtils::time_point_t start_time_wrapper( const SpecUtils::Measurement *meas )
  {
    return meas->start_time();
  }
  
  boost::python::list get_measurements_wrapper( const SpecUtils::SpecFile *info )
  {
    //This function overcomes an issue where returning a vector of
    //  std::shared_ptr<const measurement> objects to python, and then in python
    //  derefrencing an element and using it causes an issue in python to say
    //  that std::shared_ptr<const measurement> is an unknown class.
    boost::python::list l;
    for( auto p : info->measurements() )
    l.append( p );
    return l;
  }
  
  boost::python::list measurement_remarks_wrapper( const SpecUtils::Measurement *info )
  {
    boost::python::list l;
    for( const string &p : info->remarks() )
    l.append( p );
    return l;
  }
  
  
  boost::python::list SpecFile_remarks_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( const string &p : info->remarks() )
    l.append( p );
    return l;
  }

  
  boost::python::list SpecFile_parseWarnings_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( const string &p : info->parse_warnings() )
    l.append( p );
    return l;
  }


  boost::python::list gamma_channel_counts_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( size_t p : info->gamma_channel_counts() )
    l.append( p );
    return l;
  }
  
  boost::python::list sample_numbers_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( size_t p : info->sample_numbers() )
    l.append( p );
    return l;
  }
  
  boost::python::list detector_numbers_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( size_t p : info->detector_numbers() )
    l.append( p );
    return l;
  }
  
  boost::python::list neutron_detector_names_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( const string &p : info->neutron_detector_names() )
      l.append( p );
    return l;
  }
  
  boost::python::list gamma_detector_names_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( const string &p : info->gamma_detector_names() )
      l.append( p );
    return l;
  }

  boost::python::list detector_names_wrapper( const SpecUtils::SpecFile *info )
  {
    boost::python::list l;
    for( const string &p : info->detector_names() )
    l.append( p );
    return l;
  }
  
  
  std::shared_ptr<SpecUtils::Measurement> sum_measurements_wrapper( const SpecUtils::SpecFile *info,
                                                          boost::python::list py_samplenums,
                                                          boost::python::list py_detnames )
  {
    set<int> samplenums;
    set<string> detnames;
    
    boost::python::ssize_t n = boost::python::len( py_samplenums );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      samplenums.insert( boost::python::extract<int>( py_samplenums[i] ) );
    
    n = boost::python::len( py_detnames );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      detnames.insert( boost::python::extract<std::string>( py_detnames[i] ) );
    
    const vector<string> detname_vec( begin(detnames), end(detnames) );

    return info->sum_measurements( samplenums, detname_vec, nullptr );
  }//sum_measurements_wrapper(...)
  
  
  
  void writePcf_wrapper( const SpecUtils::SpecFile *info, boost::python::object pystream )
  {
    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( !info->write_pcf( output ) )
      throw std::runtime_error( "Failed to write PCF file." );
  }
  
  void write2006N42_wrapper( const SpecUtils::SpecFile *info, boost::python::object pystream )
  {
    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( !info->write_2006_N42( output ) )
      throw std::runtime_error( "Failed to write 2006 N42 file." );
  }
  
  void write2012N42Xml_wrapper( const SpecUtils::SpecFile *info, boost::python::object pystream )
  {
    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( !info->write_2012_N42( output ) )
      throw std::runtime_error( "Failed to write 2012 N42 file." );
  }
  
  void writeCsv_wrapper( const SpecUtils::SpecFile *info, boost::python::object pystream )
  {
    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( !info->write_csv( output ) )
      throw std::runtime_error( "Failed to write CSV file." );
  }
  
  void writeTxt_wrapper( const SpecUtils::SpecFile *info, boost::python::object pystream )
  {
    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( !info->write_txt( output ) )
      throw std::runtime_error( "Failed to TXT file." );
  }
  
  void writeIntegerChn_wrapper( const SpecUtils::SpecFile *info,
                               boost::python::object pystream,
                               boost::python::object py_sample_nums,
                               boost::python::object py_det_names )
  {
    std::vector<std::string> det_names;
    std::set<int> sample_nums;
    
    boost::python::list sn_list = boost::python::extract<boost::python::list>(py_sample_nums);
    boost::python::list dn_list = boost::python::extract<boost::python::list>(py_det_names);
    
    boost::python::ssize_t n = boost::python::len( sn_list );
    for( boost::python::ssize_t i = 0; i < n; ++i )
    
      sample_nums.insert( boost::python::extract<int>( sn_list[i] ) );
    
    n = boost::python::len( dn_list );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      det_names.push_back( boost::python::extract<std::string>( dn_list[i] ) );
    
    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( !info->write_integer_chn( output, sample_nums, det_names ) )
      throw std::runtime_error( "Failed to write Integer CHN file." );
  }
  
  
  void setInfoFromN42File_wrapper( SpecUtils::SpecFile *info,
                                  boost::python::object pystream )
  {
    boost::iostreams::stream<PythonInputDevice> input( pystream );
    if( !info->load_from_N42( input ) )
      throw std::runtime_error( "Failed to decode input as a valid N42 file." );
  }//setInfoFromN42File_wrapper(...)
  
  
  void setInfoFromPcfFile_wrapper( SpecUtils::SpecFile *info,
                                  boost::python::object pystream )
  {
    boost::iostreams::stream<PythonInputDevice> input( pystream );
    if( !info->load_from_pcf( input ) )
      throw std::runtime_error( "Failed to decode input as a valid PCF file." );
  }//setInfoFromPcfFile_wrapper(...)
  

  void writeAllToFile_wrapper( const SpecUtils::SpecFile *info,
                             const std::string &filename,
                             const SpecUtils::SaveSpectrumAsType type )
  {
    info->write_to_file( filename, type );
  }//setInfoFromPcfFile_wrapper(...)


  void writeToFile_wrapper( const SpecUtils::SpecFile *info,
                             const std::string &filename,
                             boost::python::object py_sample_nums,
                             boost::python::object py_det_nums,
                             const SpecUtils::SaveSpectrumAsType type )
  {
    std::vector<std::string> det_names;
    std::set<int> sample_nums, det_nums;

    boost::python::list sn_list = boost::python::extract<boost::python::list>(py_sample_nums);
    boost::python::list dn_list = boost::python::extract<boost::python::list>(py_det_nums);
    
    boost::python::ssize_t n = boost::python::len( sn_list );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      sample_nums.insert( boost::python::extract<int>( sn_list[i] ) );
    
    pyListToSampleNumsOrNames( dn_list, det_names, det_nums );

    if( det_names.size() )
      info->write_to_file( filename, sample_nums, det_names, type );
    else
      info->write_to_file( filename, sample_nums, det_nums, type );
  }//writeToFile_wrapper(...)

  void writeToStream_wrapper( const SpecUtils::SpecFile *info,
                             boost::python::object pystream,
                             boost::python::object py_sample_nums,
                             boost::python::object py_det_nums,
                             const SpecUtils::SaveSpectrumAsType type )
  {
    std::vector<std::string> det_names;
    std::set<int> sample_nums, det_nums;

    boost::python::list sn_list = boost::python::extract<boost::python::list>(py_sample_nums);
    boost::python::list dn_list = boost::python::extract<boost::python::list>(py_det_nums);
    
    boost::python::ssize_t n = boost::python::len( sn_list );
    for( boost::python::ssize_t i = 0; i < n; ++i )
      sample_nums.insert( boost::python::extract<int>( sn_list[i] ) );
    
    pyListToSampleNumsOrNames( dn_list, det_names, det_nums );

    boost::iostreams::stream<PythonOutputDevice> output( pystream );
    if( det_names.size() )
      info->write( output, sample_nums, det_names, type );
    else
      info->write( output, sample_nums, det_nums, type );
  }

  void removeMeasurement_wrapper( SpecUtils::SpecFile *info,
                             boost::python::object py_meas )
  {
    std::shared_ptr<const SpecUtils::Measurement> m = boost::python::extract<std::shared_ptr<const SpecUtils::Measurement>>(py_meas);
    info->remove_measurement( m, true );
  }

  void addMeasurement_wrapper( SpecUtils::SpecFile *info,
                             boost::python::object py_meas )
  {
    std::shared_ptr<SpecUtils::Measurement> m = boost::python::extract<std::shared_ptr<SpecUtils::Measurement>>(py_meas);
    info->add_measurement( m, true );
  }

  std::vector<std::string> to_cpp_remarks( boost::python::object py_remarks )
  {
    std::vector<std::string> remarks;
    boost::python::extract<std::string> remark_as_str(py_remarks);
    if( remark_as_str.check() )
    {
      remarks.push_back( remark_as_str );
    }else
    {
      boost::python::list remarks_list = boost::python::extract<boost::python::list>(py_remarks);
      boost::python::ssize_t n = boost::python::len( remarks_list );
      for( boost::python::ssize_t i = 0; i < n; ++i )
        remarks.push_back( boost::python::extract<std::string>( remarks_list[i] ) );
    }

    return remarks;
  }

  void setMeasurementRemarks_wrapper( SpecUtils::SpecFile *info,
                             boost::python::object py_remarks,
                             boost::python::object py_meas )
  {
    std::shared_ptr<const SpecUtils::Measurement> m = boost::python::extract<std::shared_ptr<const SpecUtils::Measurement>>(py_meas);
    std::vector<std::string> remarks = to_cpp_remarks( py_remarks );
    
    info->set_remarks( remarks, m );
  }

  void setMeasRemarks_wrapper( SpecUtils::Measurement *meas,
                             boost::python::object py_remarks )
  {
    std::vector<std::string> remarks = to_cpp_remarks( py_remarks );
    
    meas->set_remarks( remarks );
  }

  void setRemarks_wrapper( SpecUtils::SpecFile *info,
                          boost::python::object py_remarks )
  {
    std::vector<std::string> remarks;
    boost::python::extract<std::string> remark_as_str(py_remarks);
    if( remark_as_str.check() )
    {
      remarks.push_back( remark_as_str );
    }else
    {
      boost::python::list remarks_list = boost::python::extract<boost::python::list>(py_remarks);
      boost::python::ssize_t n = boost::python::len( remarks_list );
      for( boost::python::ssize_t i = 0; i < n; ++i )
        remarks.push_back( boost::python::extract<std::string>( remarks_list[i] ) );
    }
    
    info->set_remarks( remarks );
  }

  void setParseWarnings_wrapper( SpecUtils::SpecFile *info,
                          boost::python::object py_warnings )
  {
    std::vector<std::string> remarks;
    boost::python::extract<std::string> warning_as_str(py_warnings);
    if( warning_as_str.check() )
    {
      remarks.push_back( warning_as_str );
    }else
    {
      boost::python::list remarks_list = boost::python::extract<boost::python::list>(py_warnings);
      boost::python::ssize_t n = boost::python::len( remarks_list );
      for( boost::python::ssize_t i = 0; i < n; ++i )
        remarks.push_back( boost::python::extract<std::string>( remarks_list[i] ) );
    }
    
    info->set_parse_warnings( remarks );
  }

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromPolynomial_wrapper( const size_t num_channels,
                         boost::python::list py_coefs )
{
  vector<float> coefs;
  boost::python::ssize_t n = boost::python::len( py_coefs );
  for( boost::python::ssize_t i = 0; i < n; ++i )
    coefs.push_back( boost::python::extract<float>( py_coefs[i] ) );

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_polynomial( num_channels, coefs, {} );
  return cal;
}


std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromPolynomial_2_wrapper( const size_t num_channels,
                         boost::python::list py_coefs,
                         boost::python::list py_dev_pairs )
{
  vector<float> coefs;
  boost::python::ssize_t n = boost::python::len( py_coefs );
  for( boost::python::ssize_t i = 0; i < n; ++i )
    coefs.push_back( boost::python::extract<float>( py_coefs[i] ) );


  std::vector<std::pair<float,float>> dev_pairs;
  n = boost::python::len( py_dev_pairs );
  for( boost::python::ssize_t i = 0; i < n; ++i )
  {
    auto devpair = boost::python::extract<boost::python::tuple>( py_dev_pairs[i] )();
    dev_pairs.push_back( {boost::python::extract<float>(devpair[0]), boost::python::extract<float>(devpair[1]) } );
  }

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_polynomial( num_channels, coefs, dev_pairs );
  return cal;
}

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromFullRangeFraction_wrapper( const size_t num_channels,
                         boost::python::list py_coefs )
{
  vector<float> coefs;
  boost::python::ssize_t n = boost::python::len( py_coefs );
  for( boost::python::ssize_t i = 0; i < n; ++i )
    coefs.push_back( boost::python::extract<float>( py_coefs[i] ) );

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_full_range_fraction( num_channels, coefs, {} );
  return cal;
}

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromFullRangeFraction_2_wrapper( const size_t num_channels,
                         boost::python::list py_coefs,
                         boost::python::list py_dev_pairs )
{
  vector<float> coefs;
  boost::python::ssize_t n = boost::python::len( py_coefs );
  for( boost::python::ssize_t i = 0; i < n; ++i )
    coefs.push_back( boost::python::extract<float>( py_coefs[i] ) );

  std::vector<std::pair<float,float>> dev_pairs;
  n = boost::python::len( py_dev_pairs );
  for( boost::python::ssize_t i = 0; i < n; ++i )
  {
    auto devpair = boost::python::extract<boost::python::tuple>( py_dev_pairs[i] )();
    dev_pairs.push_back( {boost::python::extract<float>(devpair[0]), boost::python::extract<float>(devpair[1]) } );
  }

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_full_range_fraction( num_channels, coefs, dev_pairs );
  return cal;
}

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromLowerChannelEnergies_wrapper( const size_t num_channels,
                         const std::vector<float> energies )
{
  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_lower_channel_energy( num_channels, energies );
  return cal;
}
    

#if( SpecUtils_ENABLE_D3_CHART )
bool write_spectrum_data_js_wrapper( boost::python::object pystream,
                                    const SpecUtils::Measurement *meas,
                                    const D3SpectrumExport::D3SpectrumOptions *options,
                                    const size_t specID, const int backgroundID )
{
  boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  return D3SpectrumExport::write_spectrum_data_js( output, *meas, *options, specID, backgroundID );
}


 
bool write_d3_html_wrapper( boost::python::object pystream,
                            boost::python::list meas_list,
                            D3SpectrumExport::D3SpectrumChartOptions options )
{
  boost::iostreams::stream<PythonOutputDevice> output( pystream );
 
  // From python the list would be like: [(meas_0,options_0),(meas_1,options_1),(meas_2,options_2)]
  
  vector<pair<const SpecUtils::Measurement *,D3SpectrumExport::D3SpectrumOptions>> meass;
  
  boost::python::ssize_t n = boost::python::len(meas_list);
  for( boost::python::ssize_t i = 0; i < n; ++i )
  {
    boost::python::tuple elem = boost::python::extract<boost::python::tuple>( meas_list[i] )();
    const SpecUtils::Measurement * meas = boost::python::extract<const SpecUtils::Measurement *>( elem[0] )();
    D3SpectrumExport::D3SpectrumOptions opts = boost::python::extract<D3SpectrumExport::D3SpectrumOptions>( elem[1] )();
    meass.push_back( std::make_pair(meas,opts) );
  }
  
  return D3SpectrumExport::write_d3_html( output, meass, options );
}
 
bool write_and_set_data_for_chart_wrapper( boost::python::object pystream,
                                           const std::string &div_name,
                                           boost::python::list meas_list )
 {
   boost::iostreams::stream<PythonOutputDevice> output( pystream );
 
   vector<pair<const SpecUtils::Measurement *,D3SpectrumExport::D3SpectrumOptions>> meass;
   
   boost::python::ssize_t n = boost::python::len(meas_list);
   for( boost::python::ssize_t i = 0; i < n; ++i )
   {
     boost::python::tuple elem = boost::python::extract<boost::python::tuple>( meas_list[i] )();
     const SpecUtils::Measurement * meas = boost::python::extract<const SpecUtils::Measurement *>( elem[0] )();
     D3SpectrumExport::D3SpectrumOptions opts = boost::python::extract<D3SpectrumExport::D3SpectrumOptions>( elem[1] )();
     meass.push_back( std::make_pair(meas,opts) );
   }

   return write_and_set_data_for_chart( output, div_name, meass );
 }


bool write_html_page_header_wrapper( boost::python::object pystream, const std::string &page_title )
{
  
  
  boost::iostreams::stream<PythonOutputDevice> output( pystream );
  return D3SpectrumExport::write_html_page_header( output, page_title );
}


bool write_js_for_chart_wrapper( boost::python::object pystream, const std::string &div_name,
                        const std::string &chart_title,
                        const std::string &x_axis_title,
                        const std::string &y_axis_title )
{
  boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  return D3SpectrumExport::write_js_for_chart( output, div_name, chart_title,
                                                      x_axis_title, y_axis_title );
}



bool write_set_options_for_chart_wrapper( boost::python::object pystream,
                                          const std::string &div_name,
                                          const D3SpectrumExport::D3SpectrumChartOptions &options )
{
  boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  return write_set_options_for_chart( output, div_name, options );
}

bool write_html_display_options_for_chart_wrapper( boost::python::object pystream,
                                                   const std::string &div_name,
                                                   const D3SpectrumExport::D3SpectrumChartOptions &options )
{
  boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  return write_html_display_options_for_chart( output, div_name, options );
}
#endif
 
}//namespace



//Begin definition of python functions

//make is so loadFile will take at least 3 arguments, but can take 4 (e.g. allow
//  default last argument)
BOOST_PYTHON_FUNCTION_OVERLOADS(loadFile_overloads, loadFile, 3, 4)

BOOST_PYTHON_MODULE(SpecUtils)
{
  using namespace boost::python;
  
  //enable python signature and user document string, but disable c++ signature
  //  for all docstrings created in the scope of local_docstring_options.
  docstring_options local_docstring_options( true, true, false );
  
  //Register our enums
  enum_<SpecUtils::ParserType>( "ParserType" )
  .value( "N42_2006", SpecUtils::ParserType::N42_2006 )
  .value( "N42_2012", SpecUtils::ParserType::N42_2012 )
  .value( "Spc", SpecUtils::ParserType::Spc )
  .value( "Exploranium", SpecUtils::ParserType::Exploranium )
  .value( "Pcf", SpecUtils::ParserType::Pcf )
  .value( "Chn", SpecUtils::ParserType::Chn )
  .value( "SpeIaea", SpecUtils::ParserType::SpeIaea )
  .value( "TxtOrCsv", SpecUtils::ParserType::TxtOrCsv )
  .value( "Cnf", SpecUtils::ParserType::Cnf )
  .value( "TracsMps", SpecUtils::ParserType::TracsMps )
  .value( "SPMDailyFile", SpecUtils::ParserType::SPMDailyFile )
  .value( "AmptekMca", SpecUtils::ParserType::AmptekMca )
  .value( "MicroRaider", SpecUtils::ParserType::MicroRaider )
  .value( "RadiaCode", SpecUtils::ParserType::RadiaCode )
  .value( "OrtecListMode", SpecUtils::ParserType::OrtecListMode )
  .value( "LsrmSpe", SpecUtils::ParserType::LsrmSpe )
  .value( "Tka", SpecUtils::ParserType::Tka )
  .value( "MultiAct", SpecUtils::ParserType::MultiAct )
  .value( "Phd", SpecUtils::ParserType::Phd )
  .value( "Lzs", SpecUtils::ParserType::Lzs )
  .value( "Aram", SpecUtils::ParserType::Aram )
  .value( "ScanDataXml", SpecUtils::ParserType::ScanDataXml )
  .value( "Json", SpecUtils::ParserType::Json )
  .value( "CaenHexagonGXml", SpecUtils::ParserType::CaenHexagonGXml )
  .value( "Auto", SpecUtils::ParserType::Auto );



enum_<SpecUtils::DetectorType>( "DetectorType" )
  .value( "Exploranium", SpecUtils::DetectorType::Exploranium )
  .value( "IdentiFinder", SpecUtils::DetectorType::IdentiFinder )
  .value( "IdentiFinderNG", SpecUtils::DetectorType::IdentiFinderNG )
  .value( "IdentiFinderLaBr3", SpecUtils::DetectorType::IdentiFinderLaBr3 )
  .value( "IdentiFinderTungsten", SpecUtils::DetectorType::IdentiFinderTungsten )
  .value( "IdentiFinderR500NaI", SpecUtils::DetectorType::IdentiFinderR500NaI )
  .value( "IdentiFinderR500LaBr", SpecUtils::DetectorType::IdentiFinderR500LaBr )
  .value( "IdentiFinderUnknown", SpecUtils::DetectorType::IdentiFinderUnknown )
  .value( "DetectiveUnknown", SpecUtils::DetectorType::DetectiveUnknown )
  .value( "DetectiveEx", SpecUtils::DetectorType::DetectiveEx )
  .value( "DetectiveEx100", SpecUtils::DetectorType::DetectiveEx100 )
  .value( "DetectiveEx200", SpecUtils::DetectorType::DetectiveEx200 )
  .value( "DetectiveX", SpecUtils::DetectorType::DetectiveX )
  .value( "SAIC8", SpecUtils::DetectorType::SAIC8 )
  .value( "Falcon5000", SpecUtils::DetectorType::Falcon5000 )
  .value( "MicroDetective", SpecUtils::DetectorType::MicroDetective )
  .value( "MicroRaider", SpecUtils::DetectorType::MicroRaider )
  .value( "RadiaCode", SpecUtils::DetectorType::RadiaCode )
  .value( "Interceptor", SpecUtils::DetectorType::Interceptor )
  .value( "RadHunterNaI", SpecUtils::DetectorType::RadHunterNaI )
  .value( "RadHunterLaBr3", SpecUtils::DetectorType::RadHunterLaBr3 )
  .value( "Rsi701", SpecUtils::DetectorType::Rsi701 )
  .value( "Rsi705", SpecUtils::DetectorType::Rsi705 )
  .value( "AvidRsi", SpecUtils::DetectorType::AvidRsi )
  .value( "OrtecRadEagleNai", SpecUtils::DetectorType::OrtecRadEagleNai )
  .value( "OrtecRadEagleCeBr2Inch", SpecUtils::DetectorType::OrtecRadEagleCeBr2Inch )
  .value( "OrtecRadEagleCeBr3Inch", SpecUtils::DetectorType::OrtecRadEagleCeBr3Inch )
  .value( "OrtecRadEagleLaBr", SpecUtils::DetectorType::OrtecRadEagleLaBr )
  .value( "Sam940LaBr3", SpecUtils::DetectorType::Sam940LaBr3 )
  .value( "Sam940", SpecUtils::DetectorType::Sam940 )
  .value( "Sam945", SpecUtils::DetectorType::Sam945 )
  .value( "Srpm210", SpecUtils::DetectorType::Srpm210 )
  .value( "RIIDEyeNaI", SpecUtils::DetectorType::RIIDEyeNaI )
  .value( "RIIDEyeLaBr", SpecUtils::DetectorType::RIIDEyeLaBr )
  .value( "RadSeekerNaI", SpecUtils::DetectorType::RadSeekerNaI )
  .value( "RadSeekerLaBr", SpecUtils::DetectorType::RadSeekerLaBr )
  .value( "VerifinderNaI", SpecUtils::DetectorType::VerifinderNaI )
  .value( "VerifinderLaBr", SpecUtils::DetectorType::VerifinderLaBr )
  .value( "KromekD3S", SpecUtils::DetectorType::KromekD3S )
  .value( "Fulcrum", SpecUtils::DetectorType::Fulcrum )
  .value( "Fulcrum40h", SpecUtils::DetectorType::Fulcrum40h )
  .value( "Sam950", SpecUtils::DetectorType::Sam950 )
  .value( "Unknown", SpecUtils::DetectorType::Unknown );



  enum_<SpecUtils::SpectrumType>( "SpectrumType" )
    .value( "Foreground", SpecUtils::SpectrumType::Foreground )
    .value( "SecondForeground", SpecUtils::SpectrumType::SecondForeground )
    .value( "Background", SpecUtils::SpectrumType::Background );


enum_<SpecUtils::SaveSpectrumAsType>( "SaveSpectrumAsType" )
    .value( "Txt", SpecUtils::SaveSpectrumAsType::Txt )
    .value( "Csv", SpecUtils::SaveSpectrumAsType::Csv )
    .value( "Pcf", SpecUtils::SaveSpectrumAsType::Pcf )
    .value( "N42_2006", SpecUtils::SaveSpectrumAsType::N42_2006 )
    .value( "N42_2012", SpecUtils::SaveSpectrumAsType::N42_2012 )
    .value( "Chn", SpecUtils::SaveSpectrumAsType::Chn )
    .value( "SpcBinaryInt", SpecUtils::SaveSpectrumAsType::SpcBinaryInt )
    .value( "SpcBinaryFloat", SpecUtils::SaveSpectrumAsType::SpcBinaryFloat )
    .value( "SpcAscii", SpecUtils::SaveSpectrumAsType::SpcAscii )
    .value( "ExploraniumGr130v0", SpecUtils::SaveSpectrumAsType::ExploraniumGr130v0 )
    .value( "ExploraniumGr135v2", SpecUtils::SaveSpectrumAsType::ExploraniumGr135v2 )
    .value( "SpeIaea", SpecUtils::SaveSpectrumAsType::SpeIaea )
    .value( "Cnf", SpecUtils::SaveSpectrumAsType::Cnf )
    .value( "Tka", SpecUtils::SaveSpectrumAsType::Tka )
#if( SpecUtils_ENABLE_D3_CHART )
    .value( "HtmlD3", SpecUtils::SaveSpectrumAsType::HtmlD3 )
#endif
#if( SpecUtils_INJA_TEMPLATES )
    .value( "Template", SpecUtils::SaveSpectrumAsType::Template )
#endif
#if( SpecUtils_ENABLE_URI_SPECTRA )
    .value( "Uri", SpecUtils::SaveSpectrumAsType::Uri )
#endif
    .value( "NumTypes", SpecUtils::SaveSpectrumAsType::NumTypes );

enum_<SpecUtils::SourceType>( "SourceType" )
    .value( "Background", SpecUtils::SourceType::Background )
    .value( "Calibration", SpecUtils::SourceType::Calibration )
    .value( "Foreground", SpecUtils::SourceType::Foreground )
    .value( "IntrinsicActivity", SpecUtils::SourceType::IntrinsicActivity )
    .value( "UnknownSourceType", SpecUtils::SourceType::Unknown )
    .export_values();
    
enum_<SpecUtils::QualityStatus>( "QualityStatus" )
    .value( "Good", SpecUtils::QualityStatus::Good )
    .value( "Suspect", SpecUtils::QualityStatus::Suspect )
    .value( "Bad", SpecUtils::QualityStatus::Bad )
    .value( "Missing", SpecUtils::QualityStatus::Missing )
    .export_values();
    
enum_<SpecUtils::OccupancyStatus>( "OccupancyStatus" )
    .value( "NotOccupied", SpecUtils::OccupancyStatus::NotOccupied )
    .value( "Occupied", SpecUtils::OccupancyStatus::Occupied )
    .value( "UnknownOccupancyStatus", SpecUtils::OccupancyStatus::Unknown )
    .export_values();

enum_<SpecUtils::EnergyCalType>( "EnergyCalType" )
    .value( "Polynomial", SpecUtils::EnergyCalType::Polynomial )
    .value( "FullRangeFraction", SpecUtils::EnergyCalType::FullRangeFraction )
    .value( "LowerChannelEdge", SpecUtils::EnergyCalType::LowerChannelEdge )
    .value( "InvalidEquationType", SpecUtils::EnergyCalType::InvalidEquationType )
    .value( "UnspecifiedUsingDefaultPolynomial", SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial )
    .export_values();
    
//disambiguate the set_lower_channel_energy function, from its overload
void (SpecUtils::EnergyCalibration::*set_lower_channel_energy_fcn_ptr)( const size_t, const std::vector<float> & ) = &SpecUtils::EnergyCalibration::set_lower_channel_energy;


class_<SpecUtils::EnergyCalibration>("EnergyCalibration")
  .def( "type", &SpecUtils::EnergyCalibration::type, 
    "Returns the energy calibration type" )
  .def( "valid", &SpecUtils::EnergyCalibration::valid,
    "Returns if the energy calibration is valid." )
  .def( "coefficients", &SpecUtils::EnergyCalibration::coefficients, return_internal_reference<>(),
    "Returns the list of energy calibration coeficients.\n"
    "Will only be empty for SpecUtils.EnergyCalType.InvalidEquationType." )
  // TODO: I think we should put a wrapper around channel_energies, and return a proper python list
  .def( "channelEnergies", &SpecUtils::EnergyCalibration::channel_energies, return_internal_reference<>(),
    "Returns lower channel energies; will have one more entry than the number of channels." )
  .def( "deviationPairs", &SpecUtils::EnergyCalibration::deviation_pairs, return_internal_reference<>() )
  .def( "numChannels", &SpecUtils::EnergyCalibration::num_channels,
    "Returns the number of channels this energy calibration is for." )
  .def( "channelForEnergy", &SpecUtils::EnergyCalibration::channel_for_energy,
    "Returns channel number (as a double) for the specified energy." )
  .def( "energyForChannel", &SpecUtils::EnergyCalibration::energy_for_channel,
    "Returns energy for the specified (as double) channel number." )
  .def( "lowerEnergy", &SpecUtils::EnergyCalibration::lower_energy,
    "Returns lowest energy of this energy calibration." )
  .def( "upperEnergy", &SpecUtils::EnergyCalibration::upper_energy,
    "Returns highest energy of this energy calibration." )
  .def( "setPolynomial", &SpecUtils::EnergyCalibration::set_polynomial,
     args( "NumChannels", "Coeffiecients", "DeviationPairs" ),
    "Sets the energy calibration information from Polynomial defined coefficents." )
  .def( "setFullRangeFraction", &SpecUtils::EnergyCalibration::set_full_range_fraction,
     args( "NumChannels", "Coeffiecients", "DeviationPairs" ),
    "Sets the energy calibration information from Full Range Fraction (e.g., what PCF files use) defined coefficents." )
  .def( "setLowerChannelEnergy", set_lower_channel_energy_fcn_ptr,
     args( "NumChannels", "Energies" ),
    "Sets the energy calibration information from lower channel energies." )
  .def( "fromPolynomial", &energyCalFromPolynomial_wrapper,
    args( "NumChannels", "Coeffiecients" ), 
    "Creates a new energy calibration object from a polynomial definition." )
  .def( "fromPolynomial", &energyCalFromPolynomial_2_wrapper,
    args( "NumChannels", "Coeffiecients", "DeviationPairs" ), 
    "Creates a new energy calibration object from a polynomial definition, with some nonlinear-deviation pairs." )
  .def( "fromFullRangeFraction", &energyCalFromFullRangeFraction_wrapper,
    args( "NumChannels", "Coeffiecients" ), 
    "Creates a new energy calibration object from a Full Range Fraction definition." )
  .def( "fromFullRangeFraction", &energyCalFromFullRangeFraction_2_wrapper,
    args( "NumChannels", "Coeffiecients", "DeviationPairs" ), 
    "Creates a new energy calibration object from a Full Range Fraction definition, with some nonlinear-deviation pairs." )
  .def( "fromLowerChannelEnergies", &energyCalFromLowerChannelEnergies_wrapper,
    args( "NumChannels", "Energies" ), 
    "Creates a new energy calibration object from a lower channel energies." )
;
  


  {//begin Measurement class scope
    boost::python::scope Measurement_scope = class_<SpecUtils::Measurement, boost::noncopyable>( "Measurement" )
    .def( "liveTime", &SpecUtils::Measurement::live_time, "The live time help" )
    .def( "realTime", &SpecUtils::Measurement::real_time )
    .def( "containedNeutron", &SpecUtils::Measurement::contained_neutron )
    .def( "sampleNumber", &SpecUtils::Measurement::sample_number )
    .def( "title", &SpecUtils::Measurement::title, return_value_policy<copy_const_reference>() )
    .def( "occupied", &SpecUtils::Measurement::occupied )
    .def( "gammaCountSum", &SpecUtils::Measurement::gamma_count_sum )
    .def( "neutronCountsSum", &SpecUtils::Measurement::neutron_counts_sum )
    .def( "speed", &SpecUtils::Measurement::speed )
    .def( "latitude", &SpecUtils::Measurement::latitude )
    .def( "longitude", &SpecUtils::Measurement::longitude )
    .def( "hasGpsInfo", &SpecUtils::Measurement::has_gps_info )
    //      .def( "positionTime", &SpecUtils::Measurement::position_time, return_internal_reference<>() )
    .def( "detectorName", &SpecUtils::Measurement::detector_name, return_value_policy<copy_const_reference>() )
    .def( "detectorNumber", &SpecUtils::Measurement::detector_number )
    .def( "detectorType", &SpecUtils::Measurement::detector_type, return_value_policy<copy_const_reference>() )
    .def( "qualityStatus", &SpecUtils::Measurement::quality_status )
    .def( "sourceType", &SpecUtils::Measurement::source_type )
    .def( "energyCalibrationModel", &SpecUtils::Measurement::energy_calibration_model )
    //      .def( "remarks", &SpecUtils::Measurement::remarks, return_internal_reference<>() )
    .def( "remarks", &measurement_remarks_wrapper )
    //    .def( "startTime", &SpecUtils::Measurement::start_time, return_internal_reference<>() )
    .def( "startTime", &start_time_wrapper )
    .def( "calibrationCoeffs", &SpecUtils::Measurement::calibration_coeffs, return_internal_reference<>() )
    .def( "deviationPairs", &SpecUtils::Measurement::deviation_pairs, return_internal_reference<>() )
    .def( "channelEnergies", &channel_energies_wrapper )
    .def( "gammaCounts", &gamma_counts_wrapper )
    .def( "neutronCounts", &SpecUtils::Measurement::neutron_counts, return_internal_reference<>() )
    .def( "numGammaChannels", &SpecUtils::Measurement::num_gamma_channels )
    .def( "findGammaChannel", &SpecUtils::Measurement::find_gamma_channel )
    .def( "gammaChannelContent", &SpecUtils::Measurement::gamma_channel_content )
    .def( "gammaChannelLower", &SpecUtils::Measurement::gamma_channel_lower )
    .def( "gammaChannelCenter", &SpecUtils::Measurement::gamma_channel_center )
    .def( "gammaChannelUpper", &SpecUtils::Measurement::gamma_channel_upper )
    .def( "gammaChannelWidth", &SpecUtils::Measurement::gamma_channel_width )
    .def( "gammaIntegral", &SpecUtils::Measurement::gamma_integral )
    .def( "gammaChannelsSum", &SpecUtils::Measurement::gamma_channels_sum )
    .def( "gammaChannelCounts", &gamma_counts_wrapper )
    .def( "gammaEnergyMin", &SpecUtils::Measurement::gamma_energy_min )
    .def( "gammaEnergyMax", &SpecUtils::Measurement::gamma_energy_max )
    
    // Functionst to create new Measurment objects
    .def( "clone", &makeCopy_wrapper )
    .def( "new", &makeMeasurement_wrapper, 
      "Creates a new Measurement object, which you can add to a SpecUtils.SpecFile.\n"
      "The created object is really a std::shared_ptr<SpecUtils::Measurement> object in C++ land." )

    //... setter functions here 
    .def( "setTitle", &SpecUtils::Measurement::set_title,
      args("Title"),
      "Sets the 'Title' of the record - primarily used in PCF files,\n"
      "but will be saved in with N42 files as well." )
    .def( "setStartTime", &SpecUtils::Measurement::set_start_time,
      args("StartTime"),
      "Set the time the measurment started" )
    .def( "setRemarks", &setMeasRemarks_wrapper,
      args("RemarkList"),
      "Sets the remarks.\nTakes a single string, or a list of strings." )
    .def( "setSourceType", &SpecUtils::Measurement::set_source_type,
      args("SourceType"),
      "Sets the source type (Foreground, Background, Calibration, etc)\n"
      "for this Measurement; default is Unknown" )
    .def( "setSampleNumber", &SpecUtils::Measurement::set_sample_number,
      args("SampleNum"),
      "Sets the the sample number of this Measurement; if you add this\n"
      "Measurement to a SpecFile, this value may get overridden (see \n"
      "SpecFile.setSampleNumber(sample,meas))" )  
    .def( "setOccupancyStatus", &SpecUtils::Measurement::set_occupancy_status,
      args("Status"),
      "Sets the the Occupancy status.\n"
      "Defaults to OccupancyStatus::Unknown" )
    .def( "setDetectorName", &SpecUtils::Measurement::set_detector_name,
      args("Name"),
      "Sets the detectors name.")
    .def( "setPosition", &SpecUtils::Measurement::set_position,
      args("Longitude", "Latitude", "PositionTime"),
      "Sets the GPS coordinates .")
    .def( "setGammaCounts", &setGammaCounts_wrapper,
      args( "Counts", "LiveTime", "RealTime" ),
      "Sets the gamma counts array, as well as real and live times.\n"
      "If number of channels is not compatible with previous number of channels\n"
      "then the energy calibration will be reset as well." )
    .def( "setNeutronCounts", &setNeutronCounts_wrapper,
      args( "Counts", "LiveTime" ),
      "Sets neutron counts for this measurement.\n"
      "Takes in a list of floats corresponding to the neutron detectors for\n"
      "this gamma detector (i.e., if there are multiple He3 tubes).\n"
      "For most systems the list has just a single entry.\n"
      "If you pass in an empty list, the measurement will be set as not containing neutrons."
      " Live time (in seconds) for the neutron measurement must also be provided; if a value"
      " of zero, or negative is provided, the gamma real-time will be used instead.")
    .def( "setEnergyCalibration", &SpecUtils::Measurement::set_energy_calibration,
      args( "Cal" ),
      "Sets the energy calibration of this Measurement" )
    ;
  }//end Measurement class scope
  
  
  //Register smart pointers we will use with python.
  register_ptr_to_python<std::shared_ptr<SpecUtils::EnergyCalibration>>();
  register_ptr_to_python<std::shared_ptr<const SpecUtils::EnergyCalibration>>();
  implicitly_convertible<std::shared_ptr<SpecUtils::EnergyCalibration>, std::shared_ptr<const SpecUtils::EnergyCalibration> >();

  register_ptr_to_python<std::shared_ptr<SpecUtils::Measurement>>();
  register_ptr_to_python<std::shared_ptr<const SpecUtils::Measurement>>();
  register_ptr_to_python<std::shared_ptr<const std::vector<float>>>();

  implicitly_convertible<std::shared_ptr<SpecUtils::Measurement>, std::shared_ptr<const SpecUtils::Measurement> >();
  
  
  //Register vectors of C++ types we use to python
  class_< std::vector<float> >("FloatVec")
  .def( vector_indexing_suite<std::vector<float> >() );
  
  class_< std::vector<std::string> >("StringVec")
  .def( vector_indexing_suite<std::vector<std::string> >() );
  
  class_< std::vector<std::shared_ptr<const SpecUtils::Measurement> > >("MeasurementVec")
  .def( vector_indexing_suite<std::vector<std::shared_ptr<const SpecUtils::Measurement> > >() );
  
  
  PyDateTime_IMPORT;
  chrono_time_point_from_python_datetime();
  boost::python::to_python_converter<const SpecUtils::time_point_t, chrono_time_point_to_python_datetime>();
  
  //disambiguate a few functions that have overloads
  std::shared_ptr<const SpecUtils::Measurement> (SpecUtils::SpecFile::*meas_fcn_ptr)(size_t) const = &SpecUtils::SpecFile::measurement;
  
  class_<SpecUtils::SpecFile>("SpecFile")
  .def( "loadFile", &loadFile, loadFile_overloads(
        args( "file_name", "parser_type", "file_ending_hint" ),
        "Callling this function with parser_type==SpecUtils.ParserType.kAutoParser\n"
        "is the easiest way to load a spectrum file when you dont know the type of\n"
        "file.  The file_ending_hint is only used in the case of SpecUtils.ParserType.kAutoParser\n"
        "and uses the file ending to effect the order of parsers tried, example\n"
        "values for this might be: \"n24\", \"pcf\", \"chn\", etc. The entire filename\n"
        "can be passed in since only the letters after the last period are used.\n"
        "Throws RuntimeError if the file can not be opened or parsed." ) )
  .def( "modified", &SpecUtils::SpecFile::modified,
        "Indicates if object has been modified since last save." )
  .def( "numMeasurements", &SpecUtils::SpecFile::num_measurements,
        "Returns the number of measurements (sometimes called records) parsed." )
  .def( "measurement", meas_fcn_ptr, args("i"),
        "Returns the i'th measurement, where valid values are between 0 and\n"
        "SpecFile.numMeasurements()-1.\n"
        "Throws RuntimeError if i is out of range." )
  .def( "measurements", &get_measurements_wrapper,
        "Returns a list of all Measurement's that were parsed." )
  .def( "gammaLiveTime", &SpecUtils::SpecFile::gamma_live_time,
        "Returns the sum of detector live times of the all the parsed Measurements." )
  .def( "gammaRealTime", &SpecUtils::SpecFile::gamma_real_time,
        "Returns the sum of detector real times (wall/clock time) of the all the\n"
        "parsed Measurements." )
  .def( "gammaCountSum", &SpecUtils::SpecFile::gamma_count_sum,
        "Returns the summed number of gamma counts from all parsed Measurements." )
  .def( "neutronCountsSum", &SpecUtils::SpecFile::neutron_counts_sum,
        "Returns the summed number of neutron counts from all parsed Measurements." )
  .def( "filename", &SpecUtils::SpecFile::filename, return_value_policy<copy_const_reference>(),
        "Returns the filename of parsed file; if the \"file\" was parsed from a\n"
        "stream, then may be empty unless user specifically set it using \n"
        "setFilename (not currently implemented for python)." )
  .def( "detectorNames", &detector_names_wrapper,
        "Returns a list of names for all detectors found within the parsed file.\n"
        "The list will be in the same order as (and correspond one-to-one with)\n"
        "the list SpecFile.detectorNumbers() returns.\n"
        "Will include gamma and neutron detectors." )
  .def( "detectorNumbers", &detector_numbers_wrapper,
        "Returns a list of assigned detector numbers for all detectors found within\n"
        "the parsed file.  The list will be in the same order as (and correspond\n"
        "one-to-one with) the list SpecFile.detectorNames() returns." )
  .def( "neutronDetectorNames", &neutron_detector_names_wrapper,
        "Returns list of names of detectors that contained neutron information." )
  .def( "gammaDetectorNames", &gamma_detector_names_wrapper,
        "Returns list of names of detectors that contained gamma spectra." )
  .def( "uuid", &SpecUtils::SpecFile::uuid, return_value_policy<copy_const_reference>(),
        "Returns the unique ID string for this parsed spectrum file.  The UUID\n"
        "may have been specified in the input file itself, or if not, it is\n"
        "generated using the file contents.  This value will always be the same\n"
        "every time the file is parsed." )
  .def( "remarks", &SpecFile_remarks_wrapper,
        "Returns a list of remarks or comments found while parsing the spectrum file." )
  .def( "parseWarnings", &SpecFile_parseWarnings_wrapper,
        "Returns a list of warnings generated while parsing the input file.\n")
  .def( "laneNumber", &SpecUtils::SpecFile::lane_number,
        "Returns the lane number of the RPM if specified in the spectrum file, otherwise\n"
        "will have a value of -1." )
  .def( "measurementLocationName", &SpecUtils::SpecFile::measurement_location_name, return_value_policy<copy_const_reference>(),
        "Returns the location name specified in the spectrum file; will be an\n"
        "empty string if not specified." )
  .def( "inspection", &SpecUtils::SpecFile::inspection, return_value_policy<copy_const_reference>(),
        "Returns the inspection type (e.g. primary, secondary, etc.) specified\n"
        "in the spectrum file. If not specified an empty string will be returned." )
  .def( "measurementOperator", &SpecUtils::SpecFile::measurement_operator, return_value_policy<copy_const_reference>(),
        "Returns the detector operators name if specified in the spectrum file.\n"
        "If not specified an empty string will be returned." )
  .def( "sampleNumbers", &sample_numbers_wrapper,
        "If a spectrum file contains multiple measurements (records) from multiple\n"
        "detectors, the the measurements for the same time intervals will be grouped\n"
        "into unique groupings of sample and detectors, with the sample number\n"
        "generally increasing for measurements taken later in time.\n"
        "This function returns a list of all sample numbers in the parsed file." )
  .def( "numMeasurements", &SpecUtils::SpecFile::num_measurements,
        "Returns the number of measurements (records) parsed from the spectrum file." )
  .def( "detectorType", &SpecUtils::SpecFile::detector_type,
        "Returns the detector type specified in the spectrum file, or an empty string\n"
        "if none was specified.  Example values could include: 'HPGe 50%' or 'NaI'.")
  .def( "instrumentType", &SpecUtils::SpecFile::instrument_type, return_value_policy<copy_const_reference>(),
        "Returns the instrument type if specified in (or infered from) the spectrum\n"
        "file, or an empty string otherwise. Example values could include: PortalMonitor,\n"
        "SpecPortal, RadionuclideIdentifier, etc." )
  .def( "manufacturer", &SpecUtils::SpecFile::manufacturer, return_value_policy<copy_const_reference>(),
        "Returns the detector manufacturer if specified (or infered), or an empty\n"
        "string otherwise." )
  .def( "instrumentModel", &SpecUtils::SpecFile::instrument_model, return_value_policy<copy_const_reference>(),
        "Returns the instrument model if specified, or infered from, the spectrum file.\n"
        "Returns empty string otherwise.  Examples include: 'Falcon 5000', 'ASP', \n"
        "'identiFINDER', etc." )
  .def( "instrumentId", &SpecUtils::SpecFile::instrument_id, return_value_policy<copy_const_reference>(),
        "Returns the instrument ID (typically the serial number) specified in the\n"
        "file, or an empty string otherwise." )
  //     inline std::shared_ptr<const DetectorAnalysis> detectors_analysis() const;
  .def( "hasGpsInfo", &SpecUtils::SpecFile::has_gps_info,
        "Returns True if any of the measurements contained valid GPS data." )
  .def( "meanLatitude", &SpecUtils::SpecFile::mean_latitude,
        "Returns the mean latitidue of all measurements with valid GPS data.  If no\n"
        "GPS data was availble, will return something close to -999.9." )
  .def( "meanLongitude", &SpecUtils::SpecFile::mean_longitude,
        "Returns the mean longitude of all measurements with valid GPS data.  If no\n"
        "GPS data was availble, will return something close to -999.9." )
  .def( "passthrough", &SpecUtils::SpecFile::passthrough,
        "Returns if the file likely represents data from a RPM or search system." )
  .def( "portalOrSearch", &SpecUtils::SpecFile::passthrough,
        "Returns if the file likely represents data from a RPM or search system." )
  .def( "memmorysize", &SpecUtils::SpecFile::memmorysize,
        "Returns the approximate (lower bound) of bytes this object takes up in memory." )
  .def( "containsDerivedData", &SpecUtils::SpecFile::contains_derived_data,
        "Returns if the spectrum file contained derived data (only relevant to N42-2012 files)." )
  .def( "gammaChannelCounts", &gamma_channel_counts_wrapper,
        "Returns the set of number of channels the gamma data has. If all measurements\n"
        "in the file contained the same number of channels, then the resulting list\n"
        "will have one entry with the number of channels (so typically 1024 for Nai,\n"
        "16384 for HPGe, etc.).  If there are detectors with different numbers of bins,\n"
        "then the result returned will have multiple entries.")
  .def( "numGammaChannels", &SpecUtils::SpecFile::num_gamma_channels,
        "Returns the number of gamma channels of the first (gamma) detector found\n"
        " or 0 if there is no gamma data.")
  .def( "backgroundSampleNumber", &SpecUtils::SpecFile::background_sample_number,
        "Returns the first background sample number in the spectrum file, even if\n"
        "there is more than one background sample number." )
  .def( "reset", &SpecUtils::SpecFile::reset,
        "Resets the SpecUtils::SpecFile object to its initial (empty) state." )
  .def( "sumMeasurements", &sum_measurements_wrapper,
        args("SampleNumbers", "DetectorNames"),
        "Sums the measurements of the specified sample and detector numbers.\n"
        "SampleNumbers is a list of integers and DetectorNames is a list of strings.\n"
        "If the measurements contain different energy binnings, one will be chosen\n"
        "and the other measurements rebinned before summing so that energies stay\n"
        "consistent (e.g. not just a bin-by-bin summing).\n"
        "Throws RuntimeError if SampleNumbers or DetectorNumbers contain invalid\n"
        "entries." )
  .def( "writePcf", &writePcf_wrapper, boost::python::arg("OutputStream"),
        "The PCF format is the binary native format of GADRAS.  Saving to this format\n"
        "will cause the loss of some information. However, Calibration,\n"
        "foreground/background, speed, sample, and spectrum title (up to 60 characters)\n"
        "will be preserved along with the spectral information and neutron counts.\n"
        "Throws RuntimeError on failure."
       )
  .def( "write2006N42", &write2006N42_wrapper, args("OutputStream"),
        "Writes a 2006 version of ICD1 N42 file to OutputStream; most information\n"
        "is preserved in the output.\n"
        "Throws RuntimeError on failure." )
  .def( "writeCsv", &writeCsv_wrapper, args("OutputStream"),
        "The spectra are written out in a two column format (seperated by a comma);\n"
        "the first column is gamma channel lower edge energy, the second column is\n"
        "channel counts.  Each spectrum in the file are written out contiguously and\n"
        "seperated by a header that reads \"Energy, Data\".  Windows style line endings\n"
        "are used (\\n\\r).  This format loses all non-spectral information, including\n"
        "live and real times, and is intended to be an easy way to import the spectral\n"
        "information into other programs like Excel.\n"
        "Throws RuntimeError on write failure." )
  .def( "writeTxt", &writeTxt_wrapper, args("OutputStream"),
        "Spectrum(s) will be written to an ascii text format.  At the beggining of the\n"
        "output the original file name, total live and real times, sum gamma counts,\n"
        "sum neutron counts, and any file level remarks will be written on seperate\n"
        "labeled lines. Then after two blank lines each spectrum in the current file\n"
        "will be written, seperated by two blank lines.  Each spectrum will contain\n"
        "all remarks, measurement start time (if valid), live and real times, sample\n"
        "number, detector name, detector type, GPS coordinates/time (if valid), \n"
        "serial number (if present), energy calibration type and coefficient values,\n"
        "and neutron counts (if valid); the channel number, channel lower energy,\n"
        "and channel counts is then provided with each channel being placed on a\n"
        "seperate line and each field being seperated by a space.\n"
        "Any detector provided analysis in the original program, as well\n"
        "manufacturer, UUID, deviation pairs, lane information, location name, or\n"
        "spectrum title is lost.\n"
        "Other programs may not be able to read back in all information written to\n"
        "the txt file.\n"
        "The Windows line ending convention is used (\\n\\r).\n"
        "This is not a standard format commonly read by other programs, and is\n"
        "intended as a easily human readable summary of the spectrum file information."
        "Throws RuntimeError on failure." )
  .def( "write2012N42Xml", &write2012N42Xml_wrapper, args("OutputStream"),
        "Saves to the N42-2012 XML format.  Nearly all relevant"
        " information in most input spectrum files will also be saved in"
        " to the output stream."
        "Throws RuntimeError on failure." )
  .def( "writeIntegerChn", &writeIntegerChn_wrapper,
        args("OutputStream","SampleNumbers","DetectorNumbers"),
        "Writes an integer binary CHN file to OutputStream.  This format holds a\n"
        "single spectrum, so you must specify the sample and detector numbers you\n"
        "would like summed; if SampleNumbers or DetectorNumbers are empty, then all\n"
        "samples or detectors will be used.\n"
        "This format preserves the gamma spectrum, measurement start time, spectrum\n"
        "title (up to 63 characters), detector description, and energy calibration.\n"
        "Energy deviation pairs and neutron counts, as well as any other meta\n"
        "information is not preserved.\n"
        "SampleNumbers and DetectorNumbers are both lists of integers.\n"
        "If the measurements contain different energy binnings, one will be chosen\n"
        "and the other measurements rebinned before summing so that energies stay\n"
        "consistent (e.g. not just a bin-by-bin summing).\n"
        "Throws RuntimeError if SampleNumbers or DetectorNumbers contain invalid\n"
        "entries, or there is a error writing to OutputStream." )
  //    .def( "", &_wrapper )
  //probably write thin wrappers for the bellow
  //   enum SpcBinaryType{ IntegerSpcType, FloatSpcType };
  //   bool writeBinarySpcFile( std::ostream &ostr, const SpcBinaryType type, std::set<int> sample_nums, const std::set<int> &det_nums ) const;
  .def( "setInfoFromN42File", &setInfoFromN42File_wrapper, args("InputStream"),
        "Parses the InputStream as a N42 (2006, 2012 and HRPDS variants) spectrum\n"
        " file.\n"
        "Throws RuntimeError on parsing or data reading failure as well as reseting\n"
        "the input stream to its original position.\n"
        "InputStream must support random access seeking (one seek to end of the file\n"
        "is used to determine input size, then its reset to begining and read serially)." )
  .def( "setInfoFromPcfFile", &setInfoFromPcfFile_wrapper, args("InputStream"),
        "Parses the InputStream as a GADRAS PCF file."
        "InputStream must support random access seeking.\n"
        "Throws RuntimeError on parsing or data reading failure." )
  .def( "writeAllToFile", &writeAllToFile_wrapper, 
        args("OutputFileName", "FileFormat" ),
        "Writes the entire SpecFile data to a file at the specified path, and with the\n"
        "specified format.\n"
        "Note that for output formats that do not support multiple records, all samples\n"
        "and detectors will be summed and written as a single spectrum." )
  .def( "writeToFile", &writeToFile_wrapper, 
        args("OutputFileName", "SampleNumbers", "DetectorNamesOrNumbers", "FileFormat" ),
        "Writes the records of the specified sample numbers and detector numbers to a\n"
        "file at the specified filesystem location.\n"
        "Note that for output formats that do not support multiple records, all samples\n"
        "and detectors will be summed and written as a single spectrum." )
  .def( "writeToStream", &writeToStream_wrapper, 
        args("OutputStream", "SampleNumbers", "DetectorNamesOrNumbers", "FileFormat" ),
        "Writes the records of the specified sample numbers and detector numbers to\n"
        "the stream.\n"
        "Note that for output formats that do not support multiple records, all samples\n"
        "and detectors will be summed and written as a single spectrum." )
  //... lots more functions here
  .def( "removeMeasurement", &removeMeasurement_wrapper, 
        args("Measurement"),
        "Removes the record from the spectrum file." )
  .def( "addMeasurement", &addMeasurement_wrapper, 
        args("Measurement"),
        "Add the record to the spectrum file." )

  // Begin setters
  .def( "setFileName", &SpecUtils::SpecFile::set_filename, 
        args("Name"),
        "Sets the SpecFile internal filename variable value." )
  .def( "setRemarks", &setRemarks_wrapper, 
        args("RemarkList" ),
        "Sets the file-level remarks.\n"
        "Takes a single string, or a list of strings." )    
  .def( "setParseWarnings", &setParseWarnings_wrapper, 
        args("ParseWarningList" ),
        "Sets the parse warnings." )    
  .def( "setUuid", &SpecUtils::SpecFile::set_uuid, 
        args("uuid"),
        "Sets the UUID of the spcetrum file." )
  .def( "setLaneNumber", &SpecUtils::SpecFile::set_lane_number, 
        args("LaneNumber"),
        "Sets the lane number of the measurement." )
  .def( "setMeasurementLocationName", &SpecUtils::SpecFile::set_measurement_location_name, 
        args("Name"),
        "Sets the measurement location name (applicable only when saving to N42)." )
  .def( "setInspectionType", &SpecUtils::SpecFile::set_inspection, 
        args("InspectrionTypeString"),
        "Sets the inspection type that will go into an N42 file." )
  .def( "setInstrumentType", &SpecUtils::SpecFile::set_instrument_type, 
        args("InstrumentType"),
        "Sets the instrument type that will go into an N42 file." )
  .def( "setDetectorType", &SpecUtils::SpecFile::set_detector_type, 
        args("Type"),
        "Sets the detector type." )
  .def( "setInstrumentManufacturer", &SpecUtils::SpecFile::set_manufacturer, 
        args("Manufacturer"),
        "Sets the instrument manufacturer name." )
  .def( "setInstrumentModel", &SpecUtils::SpecFile::set_instrument_model, 
        args("Model"),
        "Sets the instrument model name." )
  .def( "setInstrumentId", &SpecUtils::SpecFile::set_instrument_id, 
        args("SerialNumber"),
        "Sets the serial number of the instrument." )
  .def( "setSerialNumber", &SpecUtils::SpecFile::set_instrument_id, 
        args("SerialNumber"),
        "Sets the serial number of the instrument." )

//  Begin modifeir 
  .def( "changeDetectorName", &SpecUtils::SpecFile::change_detector_name, 
        args("OriginalName", "NewName"),
        "Changes the name of a given detector.\n"
        "Throws exception if 'OriginalName' did not exist." )

//size_t combine_gamma_channels( const size_t ncombine, const size_t nchannels );
//size_t truncate_gamma_channels( ...)

  // Begin functions that set Measurement quantities, through thier SpecFile owner
  .def( "setLiveTime", &SpecUtils::SpecFile::set_live_time, 
        args("LiveTime", "Measurement"),
        "Sets the live time of the specified Measurement." )
  .def( "setRealTime", &SpecUtils::SpecFile::set_real_time, 
        args("RealTime", "Measurement"),
        "Sets the real time of the specified Measurement." )
  .def( "setStartTime", &SpecUtils::SpecFile::set_start_time, 
        args("StartTime", "Measurement"),
        "Sets the start time of the specified measurement." )
  .def( "setMeasurementRemarks", &setMeasurementRemarks_wrapper, 
        args("RemarkList", "Measurement"),
        "Sets the remarks the specified measurement.\n"
        "Takes a single string, or a list of strings." )
  .def( "setSourceType", &SpecUtils::SpecFile::set_source_type, 
        args("SourceType", "Measurement"),
        "Sets the SpecUtils.SourceType the specified measurement." )
  .def( "setPosition", &SpecUtils::SpecFile::set_position, 
        args("Longitude", "Latitude", "PositionTime", "Measurement"),
        "Sets the gps coordinates for a measurement" )
  .def( "setTitle", &SpecUtils::SpecFile::set_title, 
        args("Title", "Measurement"),
        "Sets the title of the specified Measurement." )
  .def( "setNeutronCounts", &SpecUtils::SpecFile::set_contained_neutrons, 
        args("ContainedNeutrons", "Counts", "Measurement"),
        "Sets the title of the specified Measurement." )
  ;
  
  
#if( SpecUtils_ENABLE_D3_CHART )
  using namespace D3SpectrumExport;

  
  // TODO: document these next member variables
  class_<D3SpectrumExport::D3SpectrumChartOptions>("D3SpectrumChartOptions")
  .def_readwrite("title", &D3SpectrumChartOptions::m_title )
  .def_readwrite("x_axis_title", &D3SpectrumChartOptions::m_xAxisTitle )
  .def_readwrite("y_axis_title", &D3SpectrumChartOptions::m_yAxisTitle )
  .def_readwrite("data_title", &D3SpectrumChartOptions::m_dataTitle )
  .def_readwrite("use_log_y_axis", &D3SpectrumChartOptions::m_useLogYAxis )
  .def_readwrite("show_vertical_grid_lines", &D3SpectrumChartOptions::m_showVerticalGridLines )
  .def_readwrite("show_horizontal_grid_lines", &D3SpectrumChartOptions::m_showHorizontalGridLines )
  .def_readwrite("legend_enabled", &D3SpectrumChartOptions::m_legendEnabled )
  .def_readwrite("compact_x_axis", &D3SpectrumChartOptions::m_compactXAxis )
  .def_readwrite("show_peak_user_labels", &D3SpectrumChartOptions::m_showPeakUserLabels )
  .def_readwrite("show_peak_energy_labels", &D3SpectrumChartOptions::m_showPeakEnergyLabels )
  .def_readwrite("show_peak_nuclide_labels", &D3SpectrumChartOptions::m_showPeakNuclideLabels )
  .def_readwrite("show_peak_nuclide_energy_labels", &D3SpectrumChartOptions::m_showPeakNuclideEnergyLabels )
  .def_readwrite("show_escape_peak_marker", &D3SpectrumChartOptions::m_showEscapePeakMarker )
  .def_readwrite("show_compton_peak_marker", &D3SpectrumChartOptions::m_showComptonPeakMarker )
  .def_readwrite("show_compton_edge_marker", &D3SpectrumChartOptions::m_showComptonEdgeMarker )
  .def_readwrite("show_sum_peak_marker", &D3SpectrumChartOptions::m_showSumPeakMarker )
  .def_readwrite("background_subtract", &D3SpectrumChartOptions::m_backgroundSubtract )
  .def_readwrite("allow_drag_roi_extent", &D3SpectrumChartOptions::m_allowDragRoiExtent )
  .def_readwrite("x_min", &D3SpectrumChartOptions::m_xMin )
  .def_readwrite("x_max", &D3SpectrumChartOptions::m_xMax )
  //.def_readwrite("m_reference_lines_json", &D3SpectrumChartOptions::m_reference_lines_json )
  ;
  
  
  class_<D3SpectrumExport::D3SpectrumOptions>("D3SpectrumOptions")
  //  .def_readwrite("peaks_json", &D3SpectrumOptions::peaks_json )
    .def_readwrite("line_color", &D3SpectrumOptions::line_color, "A valid CSS color for the line" )
    .def_readwrite("peak_color", &D3SpectrumOptions::peak_color, "A valid CSS color for the peak"  )
    .def_readwrite("title", &D3SpectrumOptions::title,
                "If empty, title from Measurement will be used, but if non-empty, will override Measurement." )
    .def_readwrite("display_scale_factor", &D3SpectrumOptions::display_scale_factor,
                 "The y-axis scale factor to use for displaying the spectrum.\n"
                 "This is typically used for live-time normalization of the background\n"
                 "spectrum to match the foreground live-time.  Ex., if background live-time\n"
                 "is twice the foreground, you would want this factor to be 0.5 (e.g., the\n"
                 "ratio of the live-times).\n"
                 "\n"
                 "Note: this value is displayed on the legend, but no where else on the\n"
                 "chart." )
    .def_readwrite("spectrum_type", &D3SpectrumOptions::spectrum_type )
  ;
  
  // TODO: document these next functions
  def( "write_spectrum_data_js", &write_spectrum_data_js_wrapper );
  def( "write_html_page_header", &write_html_page_header_wrapper );
  def( "write_js_for_chart", &write_js_for_chart_wrapper );
  def( "write_set_options_for_chart", &write_set_options_for_chart_wrapper );
  def( "write_html_display_options_for_chart", &write_html_display_options_for_chart_wrapper );
  def( "write_d3_html", &write_d3_html_wrapper );
  def( "write_and_set_data_for_chart", &write_and_set_data_for_chart_wrapper );
   
#endif
  
}//BOOST_PYTHON_MODULE(SpecUtils)
