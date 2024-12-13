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

#include <nanobind/nanobind.h>

#include <datetime.h>  // compile with -I/path/to/python/include
#include <nanobind/stl/chrono.h>


using namespace std;
namespace py = nanobind;
using namespace py::literals;

namespace
{
  //Begin structs to help convert between c++ and python
  
  class PythonInputDevice
  {
  public:
    typedef char char_type;
    //typedef boost::iostreams::seekable_device_tag category;
    
    explicit PythonInputDevice( py::object object ) : object_(object)
    {}
    
    std::streamsize write( const char *s, std::streamsize n )
    {
      return 0;
    }
    
    std::streamsize read( char_type *buffer, std::streamsize buffer_size )
    {
      py::object pyread = object_.attr( "read" );
      if( pyread.is_none() )
        throw std::runtime_error( "Python stream has no attibute 'read'" );
      
      py::object py_data = pyread( buffer_size );
      const std::string data = py::cast<std::string>( py_data );
      
      if( data.empty() && buffer_size != 0 )
        return -1;
      
      std::copy( data.begin(), data.end(), buffer );
      
      return data.size();
    }//read()
    
    
    std::streamoff seek( std::streamoff offset, std::ios_base::seekdir way )
    {
      int pway = 0;
      switch( way )
      {
        case std::ios_base::beg: pway = 0; break;
        case std::ios_base::cur: pway = 1; break;
        case std::ios_base::end: pway = 2; break;
      }//switch( way )
      
      py::object pyseek = object_.attr( "seek" );
      py::object pytell = object_.attr( "tell" );
      
      if( pyseek.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'seek'" );
      
      if( pytell.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'tell'" );
      
      const py::object pynewpos = pyseek( offset, pway );
      const py::object pyoffset = pytell();
      const std::streamoff newpos = py::cast<std::streamoff>( pyoffset );
    
      return newpos;
    }//seek()
    
  private:
    py::object object_;
  };//class PythonInputDevice
    
  
  
  /*
  class PythonOutputDevice
  {
    //see https://stackoverflow.com/questions/26033781/converting-python-io-object-to-stdostream-when-using-boostpython
  public:
    typedef char char_type;
    
    //struct category : boost::iostreams::sink_tag, boost::iostreams::flushable_tag
    //{};
    
    explicit PythonOutputDevice( py::object object ) : object_( object )
    {}
    
    std::streamsize write( const char *buffer, std::streamsize buffer_size )
    {
      py::object pywrite = object_.attr( "write" );
      
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

        // Python 3
        Py_buffer pybuffer;
        int res = PyBuffer_FillInfo(&pybuffer, 0, (void *)buff_start, nbytes_to_write, true, PyBUF_CONTIG_RO);
        if (res == -1) {
          PyErr_Print();
          throw std::runtime_error( "Python stream error writing to buffer" );
        }
        py::object data( py::handle(PyMemoryView_FromBuffer(&pybuffer)) );

        py::object pynwrote = pywrite( data );
        const std::streamsize bytes_written = py::cast<std::streamsize>( pynwrote );
        
        nwritten += bytes_written;
      }

      return nwritten;
    }
    
    bool flush()
    {
      py::object flush = object_.attr( "flush" );
      
      if( flush.is_none() )
        throw std::runtime_error( "Python stream has no attribute 'flush'" );
        
      flush();
      return true;
    }
    
  private:
    py::object object_;
  };//class PythonOutputDevice
  */
  
/*
  struct ChronoTimePointTypeCaster : nanobind::detail::type_caster<SpecUtils::time_point_t>
  {
    bool from_python( py::handle src, uint8_t flags, cleanup_list* cleanup) noexcept override {
        if (!PyDateTime_Check(src.ptr())) {
            return false; // Not a datetime object
        }

        PyDateTime_DateTime* datetime_obj = (PyDateTime_DateTime*)src.ptr();

        int year = PyDateTime_GET_YEAR(datetime_obj);
        int month = PyDateTime_GET_MONTH(datetime_obj);
        int day = PyDateTime_GET_DAY(datetime_obj);
        int hour = PyDateTime_GET_HOUR(datetime_obj);
        int minute = PyDateTime_GET_MINUTE(datetime_obj);
        int second = PyDateTime_GET_SECOND(datetime_obj);
        int microsecond = PyDateTime_GET_MICROSECOND(datetime_obj);

        std::tm timeinfo = {};
        timeinfo.tm_year = year - 1900;
        timeinfo.tm_mon = month - 1;
        timeinfo.tm_mday = day;
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = minute;
        timeinfo.tm_sec = second;

        std::time_t tt = std::mktime(&timeinfo);
        value = std::chrono::system_clock::from_time_t(tt);
        value += std::chrono::microseconds(microsecond);

        return true;
    }

    // Convert MyCustomType to Python object
    handle to_python(const SpecUtils::time_point_t& src, rv_policy policy, cleanup_list* cleanup) noexcept override {
        std::time_t tt = std::chrono::system_clock::to_time_t(src);
        std::tm* timeinfo = std::localtime(&tt);

        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(src.time_since_epoch()).count() % 1000000;

        return PyDateTime_FromDateAndTime(timeinfo->tm_year + 1900, 
                                          timeinfo->tm_mon + 1, 
                                          timeinfo->tm_mday, 
                                          timeinfo->tm_hour, 
                                          timeinfo->tm_min, 
                                          timeinfo->tm_sec, 
                                          microseconds);
    }
  };
  */
  
  
  
  void pyListToSampleNumsOrNames( const py::list &dn_list, 
                                std::vector<std::string> &det_names,
                                std::set<int> &det_nums )
  {
    det_names.clear();
    det_nums.clear();

    size_t n = dn_list.size();
    for( size_t i = 0; i < n; ++i )
    {
      if(py::isinstance<py::int_>(dn_list[i]))
      {
        det_nums.insert( py::cast<int>( dn_list[i] ) );
      }else if(py::isinstance<py::str>(dn_list[i]))
      {
        det_names.push_back( py::cast<std::string>( dn_list[i] ) );
      }else
      {
        throw std::runtime_error( "'DetectorNamesOrNumbers' must be a list of either detector numbers, or detector names." );
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
  py::list channel_energies_wrapper( const SpecUtils::Measurement *meas )
  {
    py::list l;
    if( !meas->channel_energies() )
     return l;
    
    for( auto p : *meas->channel_energies() )
      l.append( p );
    return l;
  }
  
  py::list gamma_counts_wrapper( const SpecUtils::Measurement *meas )
  {
    py::list l;
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
                              py::list py_counts,
                              const float live_time,
                              const float real_time )
  {
    auto counts = std::make_shared<vector<float>>();
    size_t n = py_counts.size();
    for( size_t i = 0; i < n; ++i )
      counts->push_back( py::cast<float>( py_counts[i] ) );

    meas->set_gamma_counts( counts, live_time, real_time );
  }

  void setNeutronCounts_wrapper( SpecUtils::Measurement *meas,
                              py::list py_counts,
                                const float live_time )
  {
    vector<float> counts;
    size_t n = py_counts.size();
    for( size_t i = 0; i < n; ++i )
      counts.push_back( py::cast<float>( py_counts[i] ) );

    meas->set_neutron_counts( counts, live_time );
  }



  SpecUtils::time_point_t start_time_wrapper( const SpecUtils::Measurement *meas )
  {
    return meas->start_time();
  }
  
  py::list get_measurements_wrapper( const SpecUtils::SpecFile *info )
  {
    //This function overcomes an issue where returning a vector of
    //  std::shared_ptr<const measurement> objects to python, and then in python
    //  derefrencing an element and using it causes an issue in python to say
    //  that std::shared_ptr<const measurement> is an unknown class.
    py::list l;
    for( auto p : info->measurements() )
    l.append( p );
    return l;
  }
  
  py::list measurement_remarks_wrapper( const SpecUtils::Measurement *info )
  {
    py::list l;
    for( const string &p : info->remarks() )
    l.append( p );
    return l;
  }
  
  
  py::list SpecFile_remarks_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( const string &p : info->remarks() )
    l.append( p );
    return l;
  }

  
  py::list SpecFile_parseWarnings_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( const string &p : info->parse_warnings() )
    l.append( p );
    return l;
  }


  py::list gamma_channel_counts_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( size_t p : info->gamma_channel_counts() )
    l.append( p );
    return l;
  }
  
  py::list sample_numbers_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( size_t p : info->sample_numbers() )
    l.append( p );
    return l;
  }
  
  py::list detector_numbers_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( size_t p : info->detector_numbers() )
    l.append( p );
    return l;
  }
  
  py::list neutron_detector_names_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( const string &p : info->neutron_detector_names() )
      l.append( p );
    return l;
  }
  
  py::list gamma_detector_names_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( const string &p : info->gamma_detector_names() )
      l.append( p );
    return l;
  }

  py::list detector_names_wrapper( const SpecUtils::SpecFile *info )
  {
    py::list l;
    for( const string &p : info->detector_names() )
    l.append( p );
    return l;
  }
  
  
  std::shared_ptr<SpecUtils::Measurement> sum_measurements_wrapper( const SpecUtils::SpecFile *info,
                                                          py::list py_samplenums,
                                                          py::list py_detnames )
  {
    set<int> samplenums;
    set<string> detnames;
    
    size_t n = py_samplenums.size();
    for( size_t i = 0; i < n; ++i )
      samplenums.insert( py::cast<int>( py_samplenums[i] ) );
    
    n = py_detnames.size();
    for( size_t i = 0; i < n; ++i )
      detnames.insert( py::cast<std::string>( py_detnames[i] ) );
    
    const vector<string> detname_vec( begin(detnames), end(detnames) );

    return info->sum_measurements( samplenums, detname_vec, nullptr );
  }//sum_measurements_wrapper(...)
  
  
  
  void writePcf_wrapper( const SpecUtils::SpecFile *info, py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( !info->write_pcf( output ) )
    //  throw std::runtime_error( "Failed to write PCF file." );
  }
  
  void write2006N42_wrapper( const SpecUtils::SpecFile *info, py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( !info->write_2006_N42( output ) )
    //  throw std::runtime_error( "Failed to write 2006 N42 file." );
  }
  
  void write2012N42Xml_wrapper( const SpecUtils::SpecFile *info, py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( !info->write_2012_N42( output ) )
    //  throw std::runtime_error( "Failed to write 2012 N42 file." );
  }
  
  void writeCsv_wrapper( const SpecUtils::SpecFile *info, py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( !info->write_csv( output ) )
    //  throw std::runtime_error( "Failed to write CSV file." );
  }
  
  void writeTxt_wrapper( const SpecUtils::SpecFile *info, py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( !info->write_txt( output ) )
    //  throw std::runtime_error( "Failed to TXT file." );
  }
  
  void writeIntegerChn_wrapper( const SpecUtils::SpecFile *info,
                               py::object pystream,
                               py::object py_sample_nums,
                               py::object py_det_names )
  {
    std::vector<std::string> det_names;
    std::set<int> sample_nums;
    
    py::list sn_list = py::cast<py::list>(py_sample_nums);
    py::list dn_list = py::cast<py::list>(py_det_names);
    
    size_t n = sn_list.size();
    for( size_t i = 0; i < n; ++i )
    
      sample_nums.insert( py::cast<int>( sn_list[i] ) );
    
    n = dn_list.size();
    for( size_t i = 0; i < n; ++i )
      det_names.push_back( py::cast<std::string>( dn_list[i] ) );
    
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( !info->write_integer_chn( output, sample_nums, det_names ) )
    //  throw std::runtime_error( "Failed to write Integer CHN file." );
  }
  
  
  void setInfoFromN42File_wrapper( SpecUtils::SpecFile *info,
                                  py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonInputDevice> input( pystream );
    //if( !info->load_from_N42( input ) )
    //  throw std::runtime_error( "Failed to decode input as a valid N42 file." );
  }//setInfoFromN42File_wrapper(...)
  
  
  void setInfoFromPcfFile_wrapper( SpecUtils::SpecFile *info,
                                  py::object pystream )
  {
    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonInputDevice> input( pystream );
    //if( !info->load_from_pcf( input ) )
    //  throw std::runtime_error( "Failed to decode input as a valid PCF file." );
  }//setInfoFromPcfFile_wrapper(...)
  

  void writeAllToFile_wrapper( const SpecUtils::SpecFile *info,
                             const std::string &filename,
                             const SpecUtils::SaveSpectrumAsType type )
  {
    info->write_to_file( filename, type );
  }//setInfoFromPcfFile_wrapper(...)


  void writeToFile_wrapper( const SpecUtils::SpecFile *info,
                             const std::string &filename,
                             py::object py_sample_nums,
                             py::object py_det_nums,
                             const SpecUtils::SaveSpectrumAsType type )
  {
    std::vector<std::string> det_names;
    std::set<int> sample_nums, det_nums;

    py::list sn_list = py::cast<py::list>(py_sample_nums);
    py::list dn_list = py::cast<py::list>(py_det_nums);
    
    size_t n = sn_list.size();
    for( size_t i = 0; i < n; ++i )
      sample_nums.insert( py::cast<int>( sn_list[i] ) );
    
    pyListToSampleNumsOrNames( dn_list, det_names, det_nums );

    if( det_names.size() )
      info->write_to_file( filename, sample_nums, det_names, type );
    else
      info->write_to_file( filename, sample_nums, det_nums, type );
  }//writeToFile_wrapper(...)

  void writeToStream_wrapper( const SpecUtils::SpecFile *info,
                             py::object pystream,
                             py::object py_sample_nums,
                             py::object py_det_nums,
                             const SpecUtils::SaveSpectrumAsType type )
  {
    std::vector<std::string> det_names;
    std::set<int> sample_nums, det_nums;

    py::list sn_list = py::cast<py::list>(py_sample_nums);
    py::list dn_list = py::cast<py::list>(py_det_nums);
    
    size_t n = sn_list.size();
    for( size_t i = 0; i < n; ++i )
      sample_nums.insert( py::cast<int>( sn_list[i] ) );
    
    pyListToSampleNumsOrNames( dn_list, det_names, det_nums );

    throw runtime_error( "not imp" );
    //boost::iostreams::stream<PythonOutputDevice> output( pystream );
    //if( det_names.size() )
    //  info->write( output, sample_nums, det_names, type );
    //else
    //  info->write( output, sample_nums, det_nums, type );
  }

  void removeMeasurement_wrapper( SpecUtils::SpecFile *info,
                             py::object py_meas )
  {
    std::shared_ptr<const SpecUtils::Measurement> m = py::cast<std::shared_ptr<const SpecUtils::Measurement>>(py_meas);
    info->remove_measurement( m, true );
  }

  void addMeasurement_wrapper( SpecUtils::SpecFile *info,
                             py::object py_meas,
                             bool doCleanup )
  {
    std::shared_ptr<SpecUtils::Measurement> m = py::cast<std::shared_ptr<SpecUtils::Measurement>>(py_meas);
    info->add_measurement( m, doCleanup );
  }

  void cleanupAfterLoad_wrapper(  SpecUtils::SpecFile *info, 
                            bool DontChangeOrReorderSamples, 
                            bool RebinToCommonBinning, 
                            bool ReorderSamplesByTime )
  {
    unsigned int flags = 0x0;

    if( DontChangeOrReorderSamples )
      flags |= SpecUtils::SpecFile::CleanupAfterLoadFlags::DontChangeOrReorderSamples;
    if( RebinToCommonBinning )
      flags |= SpecUtils::SpecFile::CleanupAfterLoadFlags::RebinToCommonBinning;
    if( ReorderSamplesByTime )
      flags |= SpecUtils::SpecFile::CleanupAfterLoadFlags::ReorderSamplesByTime;
    
    info->cleanup_after_load( flags );
  }

  std::vector<std::string> to_cpp_remarks( py::object py_remarks )
  {
    std::vector<std::string> remarks;
    if( py::isinstance<py::str>(py_remarks) )
    {
      remarks.push_back( py::cast<std::string>(py_remarks) );
    }else
    {
      py::list remarks_list = py::cast<py::list>(py_remarks);
      size_t n = remarks_list.size();
      for( size_t i = 0; i < n; ++i )
        remarks.push_back( py::cast<std::string>( remarks_list[i] ) );
    }

    return remarks;
  }

  void setMeasurementRemarks_wrapper( SpecUtils::SpecFile *info,
                             py::object py_remarks,
                             py::object py_meas )
  {
    std::shared_ptr<const SpecUtils::Measurement> m = py::cast<std::shared_ptr<const SpecUtils::Measurement>>(py_meas);
    std::vector<std::string> remarks = to_cpp_remarks( py_remarks );
    
    info->set_remarks( remarks, m );
  }

  void setMeasRemarks_wrapper( SpecUtils::Measurement *meas,
                             py::object py_remarks )
  {
    std::vector<std::string> remarks = to_cpp_remarks( py_remarks );
    
    meas->set_remarks( remarks );
  }

  void setRemarks_wrapper( SpecUtils::SpecFile *info,
                          py::object py_remarks )
  {
    std::vector<std::string> remarks = to_cpp_remarks( py_remarks );
    
    info->set_remarks( remarks );
  }

  void setParseWarnings_wrapper( SpecUtils::SpecFile *info,
                          py::object py_warnings )
  {
    std::vector<std::string> remarks = to_cpp_remarks( py_warnings );
    
    info->set_parse_warnings( remarks );
  }

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromPolynomial_wrapper( const size_t num_channels,
                         py::list py_coefs )
{
  vector<float> coefs;
  size_t n = py_coefs.size();
  for( size_t i = 0; i < n; ++i )
    coefs.push_back( py::cast<float>( py_coefs[i] ) );

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_polynomial( num_channels, coefs, {} );
  return cal;
}


std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromPolynomial_2_wrapper( const size_t num_channels,
                         py::list py_coefs,
                         py::list py_dev_pairs )
{
  vector<float> coefs;
  size_t n = py_coefs.size();
  for( size_t i = 0; i < n; ++i )
    coefs.push_back( py::cast<float>( py_coefs[i] ) );


  std::vector<std::pair<float,float>> dev_pairs;
  n = py_dev_pairs.size();
  for( size_t i = 0; i < n; ++i )
  {
    auto devpair = py::cast<py::tuple>( py_dev_pairs[i] );
    dev_pairs.push_back( {py::cast<float>(devpair[0]), py::cast<float>(devpair[1]) } );
  }

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_polynomial( num_channels, coefs, dev_pairs );
  return cal;
}

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromFullRangeFraction_wrapper( const size_t num_channels,
                         py::list py_coefs )
{
  vector<float> coefs;
  size_t n = py_coefs.size();
  for( size_t i = 0; i < n; ++i )
    coefs.push_back( py::cast<float>( py_coefs[i] ) );

  auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
  cal->set_full_range_fraction( num_channels, coefs, {} );
  return cal;
}

std::shared_ptr<SpecUtils::EnergyCalibration> energyCalFromFullRangeFraction_2_wrapper( const size_t num_channels,
                         py::list py_coefs,
                         py::list py_dev_pairs )
{
  vector<float> coefs;
  size_t n = py_coefs.size();
  for( size_t i = 0; i < n; ++i )
    coefs.push_back( py::cast<float>( py_coefs[i] ) );

  std::vector<std::pair<float,float>> dev_pairs;
  n = py_dev_pairs.size();
  for( size_t i = 0; i < n; ++i )
  {
    auto devpair = py::cast<py::tuple>( py_dev_pairs[i] );
    dev_pairs.push_back( {py::cast<float>(devpair[0]), py::cast<float>(devpair[1]) } );
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
bool write_spectrum_data_js_wrapper( py::object pystream,
                                    const SpecUtils::Measurement *meas,
                                    const D3SpectrumExport::D3SpectrumOptions *options,
                                    const size_t specID, const int backgroundID )
{
  throw runtime_error( "not imp" );
  return false;
  //boost::iostreams::stream<PythonOutputDevice> output( pystream ); 
  //return D3SpectrumExport::write_spectrum_data_js( output, *meas, *options, specID, backgroundID );
}


 
bool write_d3_html_wrapper( py::object pystream,
                            py::list meas_list,
                            D3SpectrumExport::D3SpectrumChartOptions options )
{
  throw runtime_error( "not imp" );
  return false;
  //boost::iostreams::stream<PythonOutputDevice> output( pystream );
 
  // From python the list would be like: [(meas_0,options_0),(meas_1,options_1),(meas_2,options_2)]
  
  vector<pair<const SpecUtils::Measurement *,D3SpectrumExport::D3SpectrumOptions>> meass;
  
  size_t n = meas_list.size();
  for( size_t i = 0; i < n; ++i )
  {
    py::tuple elem = py::cast<py::tuple>( meas_list[i] );
    const SpecUtils::Measurement * meas = py::cast<const SpecUtils::Measurement *>( elem[0] );
    D3SpectrumExport::D3SpectrumOptions opts = py::cast<D3SpectrumExport::D3SpectrumOptions>( elem[1] );
    meass.push_back( std::make_pair(meas,opts) );
  }
  
  //return D3SpectrumExport::write_d3_html( output, meass, options );
}
 
bool write_and_set_data_for_chart_wrapper( py::object pystream,
                                           const std::string &div_name,
                                           py::list meas_list )
 {
   //boost::iostreams::stream<PythonOutputDevice> output( pystream );
 throw runtime_error( "not imp" );
  return false;
   vector<pair<const SpecUtils::Measurement *,D3SpectrumExport::D3SpectrumOptions>> meass;
   
   size_t n = meas_list.size();
   for( size_t i = 0; i < n; ++i )
   {
     py::tuple elem = py::cast<py::tuple>( meas_list[i] );
     const SpecUtils::Measurement * meas = py::cast<const SpecUtils::Measurement *>( elem[0] );
     D3SpectrumExport::D3SpectrumOptions opts = py::cast<D3SpectrumExport::D3SpectrumOptions>( elem[1] );
     meass.push_back( std::make_pair(meas,opts) );
   }

   //return write_and_set_data_for_chart( output, div_name, meass );
 }


bool write_html_page_header_wrapper( py::object pystream, const std::string &page_title )
{
  throw runtime_error( "not imp" );
  return false;
  
  //boost::iostreams::stream<PythonOutputDevice> output( pystream );
  //return D3SpectrumExport::write_html_page_header( output, page_title );
}


bool write_js_for_chart_wrapper( py::object pystream, const std::string &div_name,
                        const std::string &chart_title,
                        const std::string &x_axis_title,
                        const std::string &y_axis_title )
{
  throw runtime_error( "not imp" );
  return false;
  //boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  //return D3SpectrumExport::write_js_for_chart( output, div_name, chart_title,
  //                                                    x_axis_title, y_axis_title );
}



bool write_set_options_for_chart_wrapper( py::object pystream,
                                          const std::string &div_name,
                                          const D3SpectrumExport::D3SpectrumChartOptions &options )
{
  throw runtime_error( "not imp" );
  return false;
  //boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  //return write_set_options_for_chart( output, div_name, options );
}

bool write_html_display_options_for_chart_wrapper( py::object pystream,
                                                   const std::string &div_name,
                                                   const D3SpectrumExport::D3SpectrumChartOptions &options )
{
  throw runtime_error( "not imp" );
  return false;
  //boost::iostreams::stream<PythonOutputDevice> output( pystream );
  
  //return write_html_display_options_for_chart( output, div_name, options );
}

#endif
 
}//namespace



//Begin definition of python functions

//make is so loadFile will take at least 3 arguments, but can take 4 (e.g. allow
//  default last argument)
//BOOST_PYTHON_FUNCTION_OVERLOADS(loadFile_overloads, loadFile, 3, 4)

NB_MODULE(SpecUtils, m) {
  using namespace py::literals;
  
  m.def("add", [](int a, int b) { return a + b; }, "a"_a, "b"_a);

  //Register our enums
  py::enum_<SpecUtils::ParserType>(m, "ParserType")
  .value("N42_2006", SpecUtils::ParserType::N42_2006)
  .value("N42_2012", SpecUtils::ParserType::N42_2012)
  .value("Spc", SpecUtils::ParserType::Spc)
  .value("Exploranium", SpecUtils::ParserType::Exploranium)
  .value("Pcf", SpecUtils::ParserType::Pcf)
  .value("Chn", SpecUtils::ParserType::Chn)
  .value("SpeIaea", SpecUtils::ParserType::SpeIaea)
  .value("TxtOrCsv", SpecUtils::ParserType::TxtOrCsv)
  .value("Cnf", SpecUtils::ParserType::Cnf)
  .value("TracsMps", SpecUtils::ParserType::TracsMps)
  .value("SPMDailyFile", SpecUtils::ParserType::SPMDailyFile)
  .value("AmptekMca", SpecUtils::ParserType::AmptekMca)
  .value("MicroRaider", SpecUtils::ParserType::MicroRaider)
  .value("RadiaCode", SpecUtils::ParserType::RadiaCode)
  .value("OrtecListMode", SpecUtils::ParserType::OrtecListMode)
  .value("LsrmSpe", SpecUtils::ParserType::LsrmSpe)
  .value("Tka", SpecUtils::ParserType::Tka)
  .value("MultiAct", SpecUtils::ParserType::MultiAct)
  .value("Phd", SpecUtils::ParserType::Phd)
  .value("Lzs", SpecUtils::ParserType::Lzs)
  .value("Aram", SpecUtils::ParserType::Aram)
  .value("ScanDataXml", SpecUtils::ParserType::ScanDataXml)
  .value("Json", SpecUtils::ParserType::Json)
  .value("CaenHexagonGXml", SpecUtils::ParserType::CaenHexagonGXml)
  .value("Auto", SpecUtils::ParserType::Auto);



py::enum_<SpecUtils::DetectorType>(m, "DetectorType")
  .value("Exploranium", SpecUtils::DetectorType::Exploranium)
  .value("IdentiFinder", SpecUtils::DetectorType::IdentiFinder)
  .value("IdentiFinderNG", SpecUtils::DetectorType::IdentiFinderNG)
  .value("IdentiFinderLaBr3", SpecUtils::DetectorType::IdentiFinderLaBr3)
  .value("IdentiFinderTungsten", SpecUtils::DetectorType::IdentiFinderTungsten)
  .value("IdentiFinderR500NaI", SpecUtils::DetectorType::IdentiFinderR500NaI)
  .value("IdentiFinderR500LaBr", SpecUtils::DetectorType::IdentiFinderR500LaBr)
  .value("IdentiFinderUnknown", SpecUtils::DetectorType::IdentiFinderUnknown)
  .value("DetectiveUnknown", SpecUtils::DetectorType::DetectiveUnknown)
  .value("DetectiveEx", SpecUtils::DetectorType::DetectiveEx)
  .value("DetectiveEx100", SpecUtils::DetectorType::DetectiveEx100)
  .value("DetectiveEx200", SpecUtils::DetectorType::DetectiveEx200)
  .value("DetectiveX", SpecUtils::DetectorType::DetectiveX)
  .value("SAIC8", SpecUtils::DetectorType::SAIC8)
  .value("Falcon5000", SpecUtils::DetectorType::Falcon5000)
  .value("MicroDetective", SpecUtils::DetectorType::MicroDetective)
  .value("MicroRaider", SpecUtils::DetectorType::MicroRaider)
  .value("RadiaCode", SpecUtils::DetectorType::RadiaCode)
  .value("Interceptor", SpecUtils::DetectorType::Interceptor)
  .value("RadHunterNaI", SpecUtils::DetectorType::RadHunterNaI)
  .value("RadHunterLaBr3", SpecUtils::DetectorType::RadHunterLaBr3)
  .value("Rsi701", SpecUtils::DetectorType::Rsi701)
  .value("Rsi705", SpecUtils::DetectorType::Rsi705)
  .value("AvidRsi", SpecUtils::DetectorType::AvidRsi)
  .value("OrtecRadEagleNai", SpecUtils::DetectorType::OrtecRadEagleNai)
  .value("OrtecRadEagleCeBr2Inch", SpecUtils::DetectorType::OrtecRadEagleCeBr2Inch)
  .value("OrtecRadEagleCeBr3Inch", SpecUtils::DetectorType::OrtecRadEagleCeBr3Inch)
  .value("OrtecRadEagleLaBr", SpecUtils::DetectorType::OrtecRadEagleLaBr)
  .value("Sam940LaBr3", SpecUtils::DetectorType::Sam940LaBr3)
  .value("Sam940", SpecUtils::DetectorType::Sam940)
  .value("Sam945", SpecUtils::DetectorType::Sam945)
  .value("Srpm210", SpecUtils::DetectorType::Srpm210)
  .value("RIIDEyeNaI", SpecUtils::DetectorType::RIIDEyeNaI)
  .value("RIIDEyeLaBr", SpecUtils::DetectorType::RIIDEyeLaBr)
  .value("RadSeekerNaI", SpecUtils::DetectorType::RadSeekerNaI)
  .value("RadSeekerLaBr", SpecUtils::DetectorType::RadSeekerLaBr)
  .value("VerifinderNaI", SpecUtils::DetectorType::VerifinderNaI)
  .value("VerifinderLaBr", SpecUtils::DetectorType::VerifinderLaBr)
  .value("KromekD3S", SpecUtils::DetectorType::KromekD3S)
  .value("Fulcrum", SpecUtils::DetectorType::Fulcrum)
  .value("Fulcrum40h", SpecUtils::DetectorType::Fulcrum40h)
  .value("Sam950", SpecUtils::DetectorType::Sam950)
  .value("Unknown", SpecUtils::DetectorType::Unknown);



  py::enum_<SpecUtils::SpectrumType>(m, "SpectrumType")
    .value("Foreground", SpecUtils::SpectrumType::Foreground)
    .value("SecondForeground", SpecUtils::SpectrumType::SecondForeground)
    .value("Background", SpecUtils::SpectrumType::Background);


py::enum_<SpecUtils::SaveSpectrumAsType>(m, "SaveSpectrumAsType")
    .value("Txt", SpecUtils::SaveSpectrumAsType::Txt)
    .value("Csv", SpecUtils::SaveSpectrumAsType::Csv)
    .value("Pcf", SpecUtils::SaveSpectrumAsType::Pcf)
    .value("N42_2006", SpecUtils::SaveSpectrumAsType::N42_2006)
    .value("N42_2012", SpecUtils::SaveSpectrumAsType::N42_2012)
    .value("Chn", SpecUtils::SaveSpectrumAsType::Chn)
    .value("SpcBinaryInt", SpecUtils::SaveSpectrumAsType::SpcBinaryInt)
    .value("SpcBinaryFloat", SpecUtils::SaveSpectrumAsType::SpcBinaryFloat)
    .value("SpcAscii", SpecUtils::SaveSpectrumAsType::SpcAscii)
    .value("ExploraniumGr130v0", SpecUtils::SaveSpectrumAsType::ExploraniumGr130v0)
    .value("ExploraniumGr135v2", SpecUtils::SaveSpectrumAsType::ExploraniumGr135v2)
    .value("SpeIaea", SpecUtils::SaveSpectrumAsType::SpeIaea)
    .value("Cnf", SpecUtils::SaveSpectrumAsType::Cnf)
    .value("Tka", SpecUtils::SaveSpectrumAsType::Tka)
#if( SpecUtils_ENABLE_D3_CHART )
    .value("HtmlD3", SpecUtils::SaveSpectrumAsType::HtmlD3)
#endif
#if( SpecUtils_INJA_TEMPLATES )
    .value("Template", SpecUtils::SaveSpectrumAsType::Template)
#endif
#if( SpecUtils_ENABLE_URI_SPECTRA )
    .value("Uri", SpecUtils::SaveSpectrumAsType::Uri)
#endif
    .value("NumTypes", SpecUtils::SaveSpectrumAsType::NumTypes);

py::enum_<SpecUtils::SourceType>(m, "SourceType")
    .value("Background", SpecUtils::SourceType::Background)
    .value("Calibration", SpecUtils::SourceType::Calibration)
    .value("Foreground", SpecUtils::SourceType::Foreground)
    .value("IntrinsicActivity", SpecUtils::SourceType::IntrinsicActivity)
    .value("UnknownSourceType", SpecUtils::SourceType::Unknown)
    .export_values();
    
py::enum_<SpecUtils::QualityStatus>(m, "QualityStatus")
    .value("Good", SpecUtils::QualityStatus::Good)
    .value("Suspect", SpecUtils::QualityStatus::Suspect)
    .value("Bad", SpecUtils::QualityStatus::Bad)
    .value("Missing", SpecUtils::QualityStatus::Missing)
    .export_values();
    
py::enum_<SpecUtils::OccupancyStatus>(m, "OccupancyStatus")
    .value("NotOccupied", SpecUtils::OccupancyStatus::NotOccupied)
    .value("Occupied", SpecUtils::OccupancyStatus::Occupied)
    .value("UnknownOccupancyStatus", SpecUtils::OccupancyStatus::Unknown)
    .export_values();

py::enum_<SpecUtils::EnergyCalType>(m, "EnergyCalType")
    .value("Polynomial", SpecUtils::EnergyCalType::Polynomial)
    .value("FullRangeFraction", SpecUtils::EnergyCalType::FullRangeFraction)
    .value("LowerChannelEdge", SpecUtils::EnergyCalType::LowerChannelEdge)
    .value("InvalidEquationType", SpecUtils::EnergyCalType::InvalidEquationType)
    .value("UnspecifiedUsingDefaultPolynomial", SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial)
    .export_values();

/*
//disambiguate the set_lower_channel_energy function, from its overload
void (SpecUtils::EnergyCalibration::*set_lower_channel_energy_fcn_ptr)( const size_t, const std::vector<float> & ) = &SpecUtils::EnergyCalibration::set_lower_channel_energy;


py::class_<SpecUtils::EnergyCalibration>(m, "EnergyCalibration")
  .def( "type", &SpecUtils::EnergyCalibration::type, 
    "Returns the energy calibration type" )
  .def( "valid", &SpecUtils::EnergyCalibration::valid,
    "Returns if the energy calibration is valid." )
  .def( "coefficients", &SpecUtils::EnergyCalibration::coefficients, py::return_value_policy::reference,
    "Returns the list of energy calibration coeficients.\n"
    "Will only be empty for SpecUtils.EnergyCalType.InvalidEquationType." )
  // TODO: I think we should put a wrapper around channel_energies, and return a proper python list
  .def( "channelEnergies", &SpecUtils::EnergyCalibration::channel_energies, py::return_value_policy::reference,
    "Returns lower channel energies; will have one more entry than the number of channels." )
  .def( "deviationPairs", &SpecUtils::EnergyCalibration::deviation_pairs, py::return_value_policy::reference )
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
     "NumChannels"_a, "Coeffiecients"_a, "DeviationPairs"_a,
    "Sets the energy calibration information from Polynomial defined coefficents." )
  .def( "setFullRangeFraction", &SpecUtils::EnergyCalibration::set_full_range_fraction,
     "NumChannels"_a, "Coeffiecients"_a, "DeviationPairs"_a,
    "Sets the energy calibration information from Full Range Fraction (e.g., what PCF files use) defined coefficents." )
  .def( "setLowerChannelEnergy", set_lower_channel_energy_fcn_ptr,
     "NumChannels"_a, "Energies"_a,
    "Sets the energy calibration information from lower channel energies." )
  .def_static( "fromPolynomial", &energyCalFromPolynomial_wrapper,
    "NumChannels"_a, "Coeffiecients"_a, 
    "Creates a new energy calibration object from a polynomial definition." )
  .def_static( "fromPolynomial", &energyCalFromPolynomial_2_wrapper,
    "NumChannels"_a, "Coeffiecients"_a, "DeviationPairs"_a, 
    "Creates a new energy calibration object from a polynomial definition, with some nonlinear-deviation pairs." )
  .def_static( "fromFullRangeFraction", &energyCalFromFullRangeFraction_wrapper,
    "NumChannels"_a, "Coeffiecients"_a, 
    "Creates a new energy calibration object from a Full Range Fraction definition." )
  .def_static( "fromFullRangeFraction", &energyCalFromFullRangeFraction_2_wrapper,
    "NumChannels"_a, "Coeffiecients"_a, "DeviationPairs"_a, 
    "Creates a new energy calibration object from a Full Range Fraction definition, with some nonlinear-deviation pairs." )
  .def_static( "fromLowerChannelEnergies", &energyCalFromLowerChannelEnergies_wrapper,
    "NumChannels"_a, "Energies"_a, 
    "Creates a new energy calibration object from a lower channel energies." )
;


  {//begin Measurement class scope
    py::class_<SpecUtils::Measurement, py::noncopyable>(m, "Measurement")
    .def( "liveTime", &SpecUtils::Measurement::live_time, "The live time help" )
    .def( "realTime", &SpecUtils::Measurement::real_time )
    .def( "containedNeutron", &SpecUtils::Measurement::contained_neutron )
    .def( "sampleNumber", &SpecUtils::Measurement::sample_number )
    .def( "title", &SpecUtils::Measurement::title, py::return_value_policy::copy )
    .def( "occupied", &SpecUtils::Measurement::occupied )
    .def( "gammaCountSum", &SpecUtils::Measurement::gamma_count_sum )
    .def( "neutronCountsSum", &SpecUtils::Measurement::neutron_counts_sum )
    .def( "speed", &SpecUtils::Measurement::speed )
    .def( "latitude", &SpecUtils::Measurement::latitude )
    .def( "longitude", &SpecUtils::Measurement::longitude )
    .def( "hasGpsInfo", &SpecUtils::Measurement::has_gps_info )
    .def( "detectorName", &SpecUtils::Measurement::detector_name, py::return_value_policy::copy )
    .def( "detectorNumber", &SpecUtils::Measurement::detector_number )
    .def( "detectorType", &SpecUtils::Measurement::detector_type, py::return_value_policy::copy )
    .def( "qualityStatus", &SpecUtils::Measurement::quality_status )
    .def( "sourceType", &SpecUtils::Measurement::source_type )
    .def( "energyCalibrationModel", &SpecUtils::Measurement::energy_calibration_model )
    .def( "remarks", &measurement_remarks_wrapper )
    .def( "startTime", &start_time_wrapper )
    .def( "calibrationCoeffs", &SpecUtils::Measurement::calibration_coeffs, py::return_value_policy::reference )
    .def( "deviationPairs", &SpecUtils::Measurement::deviation_pairs, py::return_value_policy::reference )
    .def( "channelEnergies", &channel_energies_wrapper )
    .def( "gammaCounts", &gamma_counts_wrapper )
    .def( "neutronCounts", &SpecUtils::Measurement::neutron_counts, py::return_value_policy::reference )
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
    .def_static( "new", &makeMeasurement_wrapper, 
      "Creates a new Measurement object, which you can add to a SpecUtils.SpecFile.\n"
      "The created object is really a std::shared_ptr<SpecUtils::Measurement> object in C++ land." )

    //... setter functions here 
    .def( "setTitle", &SpecUtils::Measurement::set_title,
      "Title"_a,
      "Sets the 'Title' of the record - primarily used in PCF files,\n"
      "but will be saved in with N42 files as well." )
    .def( "setStartTime", &SpecUtils::Measurement::set_start_time,
      "StartTime"_a,
      "Set the time the measurment started" )
    .def( "setRemarks", &setMeasRemarks_wrapper,
      "RemarkList"_a,
      "Sets the remarks.\nTakes a single string, or a list of strings." )
    .def( "setSourceType", &SpecUtils::Measurement::set_source_type,
      "SourceType"_a,
      "Sets the source type (Foreground, Background, Calibration, etc)\n"
      "for this Measurement; default is Unknown" )
    .def( "setSampleNumber", &SpecUtils::Measurement::set_sample_number,
      "SampleNum"_a,
      "Sets the the sample number of this Measurement; if you add this\n"
      "Measurement to a SpecFile, this value may get overridden (see \n"
      "SpecFile.setSampleNumber(sample,meas))" )  
    .def( "setOccupancyStatus", &SpecUtils::Measurement::set_occupancy_status,
      "Status"_a,
      "Sets the the Occupancy status.\n"
      "Defaults to OccupancyStatus::Unknown" )
    .def( "setDetectorName", &SpecUtils::Measurement::set_detector_name,
      "Name"_a,
      "Sets the detectors name.")
    .def( "setPosition", &SpecUtils::Measurement::set_position,
      "Longitude"_a, "Latitude"_a, "PositionTime"_a,
      "Sets the GPS coordinates .")
    .def( "setGammaCounts", &setGammaCounts_wrapper,
      "Counts"_a, "LiveTime"_a, "RealTime"_a,
      "Sets the gamma counts array, as well as real and live times.\n"
      "If number of channels is not compatible with previous number of channels\n"
      "then the energy calibration will be reset as well." )
    .def( "setNeutronCounts", &setNeutronCounts_wrapper,
      "Counts"_a, "LiveTime"_a,
      "Sets neutron counts for this measurement.\n"
      "Takes in a list of floats corresponding to the neutron detectors for\n"
      "this gamma detector (i.e., if there are multiple He3 tubes).\n"
      "For most systems the list has just a single entry.\n"
      "If you pass in an empty list, the measurement will be set as not containing neutrons."
      " Live time (in seconds) for the neutron measurement must also be provided; if a value"
      " of zero, or negative is provided, the gamma real-time will be used instead.")
    .def( "setEnergyCalibration", &SpecUtils::Measurement::set_energy_calibration,
      "Cal"_a,
      "Sets the energy calibration of this Measurement" )
    ;
  }//end Measurement class scope

  
  
  //Register smart pointers we will use with python.
  py::class_<SpecUtils::EnergyCalibration, std::shared_ptr<SpecUtils::EnergyCalibration>>(m, "EnergyCalibration");
  py::class_<SpecUtils::Measurement, std::shared_ptr<SpecUtils::Measurement>, py::noncopyable>(m, "Measurement");
  py::class_<std::vector<float>>(m, "FloatVec");
  py::class_<std::vector<std::string>>(m, "StringVec");
  py::class_<std::vector<std::shared_ptr<const SpecUtils::Measurement>>>(m, "MeasurementVec");

  PyDateTime_IMPORT;
  chrono_time_point_from_python_datetime();
  py::implicitly_convertible<chrono::system_clock::time_point, SpecUtils::time_point_t>();

  //disambiguate a few functions that have overloads
  std::shared_ptr<const SpecUtils::Measurement> (SpecUtils::SpecFile::*meas_fcn_ptr)(size_t) const = &SpecUtils::SpecFile::measurement;
  
  py::class_<SpecUtils::SpecFile>(m, "SpecFile")
  .def( "loadFile", &loadFile, 
        "file_name"_a, "parser_type"_a, "file_ending_hint"_a = "",
        "Callling this function with parser_type==SpecUtils.ParserType.kAutoParser\n"
        "is the easiest way to load a spectrum file when you dont know the type of\n"
        "file.  The file_ending_hint is only used in the case of SpecUtils.ParserType.kAutoParser\n"
        "and uses the file ending to effect the order of parsers tried, example\n"
        "values for this might be: \"n24\", \"pcf\", \"chn\", etc. The entire filename\n"
        "can be passed in since only the letters after the last period are used.\n"
        "Throws RuntimeError if the file can not be opened or parsed." ) 
  .def( "modified", &SpecUtils::SpecFile::modified,
        "Indicates if object has been modified since last save." )
  .def( "numMeasurements", &SpecUtils::SpecFile::num_measurements,
        "Returns the number of measurements (sometimes called records) parsed." )
  .def( "measurement", meas_fcn_ptr, "i"_a,
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
  .def( "filename", &SpecUtils::SpecFile::filename, py::return_value_policy::copy,
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
  .def( "uuid", &SpecUtils::SpecFile::uuid, py::return_value_policy::copy,
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
  .def( "measurementLocationName", &SpecUtils::SpecFile::measurement_location_name, py::return_value_policy::copy,
        "Returns the location name specified in the spectrum file; will be an\n"
        "empty string if not specified." )
  .def( "inspection", &SpecUtils::SpecFile::inspection, py::return_value_policy::copy,
        "Returns the inspection type (e.g. primary, secondary, etc.) specified\n"
        "in the spectrum file. If not specified an empty string will be returned." )
  .def( "measurementOperator", &SpecUtils::SpecFile::measurement_operator, py::return_value_policy::copy,
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
  .def( "instrumentType", &SpecUtils::SpecFile::instrument_type, py::return_value_policy::copy,
        "Returns the instrument type if specified in (or infered from) the spectrum\n"
        "file, or an empty string otherwise. Example values could include: PortalMonitor,\n"
        "SpecPortal, RadionuclideIdentifier, etc." )
  .def( "manufacturer", &SpecUtils::SpecFile::manufacturer, py::return_value_policy::copy,
        "Returns the detector manufacturer if specified (or infered), or an empty\n"
        "string otherwise." )
  .def( "instrumentModel", &SpecUtils::SpecFile::instrument_model, py::return_value_policy::copy,
        "Returns the instrument model if specified, or infered from, the spectrum file.\n"
        "Returns empty string otherwise.  Examples include: 'Falcon 5000', 'ASP', \n"
        "'identiFINDER', etc." )
  .def( "instrumentId", &SpecUtils::SpecFile::instrument_id, py::return_value_policy::copy,
        "Returns the instrument ID (typically the serial number) specified in the\n"
        "file, or an empty string otherwise." )
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
        "SampleNumbers"_a, "DetectorNames"_a,
        "Sums the measurements of the specified sample and detector numbers.\n"
        "SampleNumbers is a list of integers and DetectorNames is a list of strings.\n"
        "If the measurements contain different energy binnings, one will be chosen\n"
        "and the other measurements rebinned before summing so that energies stay\n"
        "consistent (e.g. not just a bin-by-bin summing).\n"
        "Throws RuntimeError if SampleNumbers or DetectorNumbers contain invalid\n"
        "entries." )
  .def( "writePcf", &writePcf_wrapper, "OutputStream"_a,
        "The PCF format is the binary native format of GADRAS.  Saving to this format\n"
        "will cause the loss of some information. However, Calibration,\n"
        "foreground/background, speed, sample, and spectrum title (up to 60 characters)\n"
        "will be preserved along with the spectral information and neutron counts.\n"
        "Throws RuntimeError on failure."
       )
  .def( "write2006N42", &write2006N42_wrapper, "OutputStream"_a,
        "Writes a 2006 version of ICD1 N42 file to OutputStream; most information\n"
        "is preserved in the output.\n"
        "Throws RuntimeError on failure." )
  .def( "writeCsv", &writeCsv_wrapper, "OutputStream"_a,
        "The spectra are written out in a two column format (separated by a comma);\n"
        "the first column is gamma channel lower edge energy, the second column is\n"
        "channel counts.  Each spectrum in the file are written out contiguously and\n"
        "separated by a header that reads \"Energy, Data\".  Windows style line endings\n"
        "are used (\\n\\r).  This format loses all non-spectral information, including\n"
        "live and real times, and is intended to be an easy way to import the spectral\n"
        "information into other programs like Excel.\n"
        "Throws RuntimeError on write failure." )
  .def( "writeTxt", &writeTxt_wrapper, "OutputStream"_a,
        "Spectrum(s) will be written to an ascii text format.  At the beginning of the\n"
        "output the original file name, total live and real times, sum gamma counts,\n"
        "sum neutron counts, and any file level remarks will be written on separate\n"
        "labeled lines. Then after two blank lines each spectrum in the current file\n"
        "will be written, separated by two blank lines.  Each spectrum will contain\n"
        "all remarks, measurement start time (if valid), live and real times, sample\n"
        "number, detector name, detector type, GPS coordinates/time (if valid), \n"
        "serial number (if present), energy calibration type and coefficient values,\n"
        "and neutron counts (if valid); the channel number, channel lower energy,\n"
        "and channel counts is then provided with each channel being placed on a\n"
        "separate line and each field being separated by a space.\n"
        "Any detector provided analysis in the original program, as well\n"
        "manufacturer, UUID, deviation pairs, lane information, location name, or\n"
        "spectrum title is lost.\n"
        "Other programs may not be able to read back in all information written to\n"
        "the txt file.\n"
        "The Windows line ending convention is used (\\n\\r).\n"
        "This is not a standard format commonly read by other programs, and is\n"
        "intended as a easily human readable summary of the spectrum file information."
        "Throws RuntimeError on failure." )
  .def( "write2012N42Xml", &write2012N42Xml_wrapper, "OutputStream"_a,
        "Saves to the N42-2012 XML format.  Nearly all relevant"
        " information in most input spectrum files will also be saved in"
        " to the output stream."
        "Throws RuntimeError on failure." )
  .def( "writeIntegerChn", &writeIntegerChn_wrapper,
        "OutputStream"_a,"SampleNumbers"_a,"DetectorNumbers"_a,
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
  .def( "setInfoFromN42File", &setInfoFromN42File_wrapper, "InputStream"_a,
        "Parses the InputStream as a N42 (2006, 2012 and HRPDS variants) spectrum\n"
        " file.\n"
        "Throws RuntimeError on parsing or data reading failure as well as reseting\n"
        "the input stream to its original position.\n"
        "InputStream must support random access seeking (one seek to end of the file\n"
        "is used to determine input size, then its reset to begining and read serially)." )
  .def( "setInfoFromPcfFile", &setInfoFromPcfFile_wrapper, "InputStream"_a,
        "Parses the InputStream as a GADRAS PCF file."
        "InputStream must support random access seeking.\n"
        "Throws RuntimeError on parsing or data reading failure." )
    .def( "writeAllToFile", &writeAllToFile_wrapper, 
        "OutputFileName"_a, "FileFormat"_a,
        "Writes the entire SpecFile data to a file at the specified path, and with the\n"
        "specified format.\n"
        "Note that for output formats that do not support multiple records, all samples\n"
        "and detectors will be summed and written as a single spectrum." )
  .def( "writeToFile", &writeToFile_wrapper, 
        "OutputFileName"_a, "SampleNumbers"_a, "DetectorNamesOrNumbers"_a, "FileFormat"_a,
        "Writes the records of the specified sample numbers and detector numbers to a\n"
        "file at the specified filesystem location.\n"
        "Note that for output formats that do not support multiple records, all samples\n"
        "and detectors will be summed and written as a single spectrum." )
  .def( "writeToStream", &writeToStream_wrapper, 
        "OutputStream"_a, "SampleNumbers"_a, "DetectorNamesOrNumbers"_a, "FileFormat"_a,
        "Writes the records of the specified sample numbers and detector numbers to\n"
        "the stream.\n"
        "Note that for output formats that do not support multiple records, all samples\n"
        "and detectors will be summed and written as a single spectrum." )
  //... lots more functions here
  .def( "removeMeasurement", &removeMeasurement_wrapper, 
        "Measurement"_a,
        "Removes the record from the spectrum file." )
  .def( "addMeasurement", &addMeasurement_wrapper, 
        "Measurement"_a, "DoCleanup"_a,
        "Add the record to the spectrum file\n."
        "If DoCleanup is true, spectrum file sums will be computed, and possibly re-order\n"
        "measurements.  If false, then you must call cleanupAfterLoad() when you are done\n"
        "adding measurements." )
  .def( "cleanupAfterLoad", cleanupAfterLoad_wrapper,
        "DontChangeOrReorderSamples"_a, "RebinToCommonBinning"_a, "ReorderSamplesByTime"_a,
        ""
  )


    // Begin setters
  .def( "setFileName", &SpecUtils::SpecFile::set_filename, 
        "Name"_a,
        "Sets the SpecFile internal filename variable value." )
  .def( "setRemarks", &setRemarks_wrapper, 
        "RemarkList"_a,
        "Sets the file-level remarks.\n"
        "Takes a single string, or a list of strings." )    
  .def( "setParseWarnings", &setParseWarnings_wrapper, 
        "ParseWarningList"_a,
        "Sets the parse warnings." )    
  .def( "setUuid", &SpecUtils::SpecFile::set_uuid, 
        "uuid"_a,
        "Sets the UUID of the spcetrum file." )
  .def( "setLaneNumber", &SpecUtils::SpecFile::set_lane_number, 
        "LaneNumber"_a,
        "Sets the lane number of the measurement." )
  .def( "setMeasurementLocationName", &SpecUtils::SpecFile::set_measurement_location_name, 
        "Name"_a,
        "Sets the measurement location name (applicable only when saving to N42)." )
  .def( "setInspectionType", &SpecUtils::SpecFile::set_inspection, 
        "InspectrionTypeString"_a,
        "Sets the inspection type that will go into an N42 file." )
  .def( "setInstrumentType", &SpecUtils::SpecFile::set_instrument_type, 
        "InstrumentType"_a,
        "Sets the instrument type that will go into an N42 file." )
  .def( "setDetectorType", &SpecUtils::SpecFile::set_detector_type, 
        "Type"_a,
        "Sets the detector type." )
  .def( "setInstrumentManufacturer", &SpecUtils::SpecFile::set_manufacturer, 
        "Manufacturer"_a,
        "Sets the instrument manufacturer name." )
  .def( "setInstrumentModel", &SpecUtils::SpecFile::set_instrument_model, 
        "Model"_a,
        "Sets the instrument model name." )
  .def( "setInstrumentId", &SpecUtils::SpecFile::set_instrument_id, 
        "SerialNumber"_a,
        "Sets the serial number of the instrument." )
  .def( "setSerialNumber", &SpecUtils::SpecFile::set_instrument_id, 
        "SerialNumber"_a,
        "Sets the serial number of the instrument." )

//  Begin modifeir 
  .def( "changeDetectorName", &SpecUtils::SpecFile::change_detector_name, 
        "OriginalName"_a, "NewName"_a,
        "Changes the name of a given detector.\n"
        "Throws exception if 'OriginalName' did not exist." )

//size_t combine_gamma_channels( const size_t ncombine, const size_t nchannels );
//size_t truncate_gamma_channels( ...)

  // Begin functions that set Measurement quantities, through thier SpecFile owner
  .def( "setLiveTime", &SpecUtils::SpecFile::set_live_time, 
        "LiveTime"_a, "Measurement"_a,
        "Sets the live time of the specified Measurement." )
  .def( "setRealTime", &SpecUtils::SpecFile::set_real_time, 
        "RealTime"_a, "Measurement"_a,
        "Sets the real time of the specified Measurement." )
  .def( "setStartTime", &SpecUtils::SpecFile::set_start_time, 
        "StartTime"_a, "Measurement"_a,
        "Sets the start time of the specified measurement." )
  .def( "setMeasurementRemarks", &setMeasurementRemarks_wrapper, 
        "RemarkList"_a, "Measurement"_a,
        "Sets the remarks the specified measurement.\n"
        "Takes a single string, or a list of strings." )
  .def( "setSourceType", &SpecUtils::SpecFile::set_source_type, 
        "SourceType"_a, "Measurement"_a,
        "Sets the SpecUtils.SourceType the specified measurement." )
  .def( "setPosition", &SpecUtils::SpecFile::set_position, 
        "Longitude"_a, "Latitude"_a, "PositionTime"_a, "Measurement"_a,
        "Sets the gps coordinates for a measurement" )
  .def( "setTitle", &SpecUtils::SpecFile::set_title, 
        "Title"_a, "Measurement"_a,
        "Sets the title of the specified Measurement." )
  .def( "setNeutronCounts", &SpecUtils::SpecFile::set_contained_neutrons, 
        "ContainedNeutrons"_a, "Counts"_a, "Measurement"_a,
        "Sets the title of the specified Measurement." )
  ;

  
  
#if( SpecUtils_ENABLE_D3_CHART )
  using namespace D3SpectrumExport;

  
  // TODO: document these next member variables
  py::class_<D3SpectrumExport::D3SpectrumChartOptions>(m, "D3SpectrumChartOptions")
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
  
  
  py::class_<D3SpectrumExport::D3SpectrumOptions>(m, "D3SpectrumOptions")
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
  m.def( "write_spectrum_data_js", &write_spectrum_data_js_wrapper );
  m.def( "write_html_page_header", &write_html_page_header_wrapper );
  m.def( "write_js_for_chart", &write_js_for_chart_wrapper );
  m.def( "write_set_options_for_chart", &write_set_options_for_chart_wrapper );
  m.def( "write_html_display_options_for_chart", &write_html_display_options_for_chart_wrapper );
  m.def( "write_d3_html", &write_d3_html_wrapper );
  m.def( "write_and_set_data_for_chart", &write_and_set_data_for_chart_wrapper );
   

#endif
  */
}//BOOST_PYTHON_MODULE(SpecUtils)
