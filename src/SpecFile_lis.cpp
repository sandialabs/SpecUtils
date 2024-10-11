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

#include "SpecUtils_config.h"

#include <math.h>
#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <numeric>
#include <iostream>
#include <stdexcept>

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
  SpecUtils::time_point_t datetime_ole_to_posix( const double ole_dt )
  {
    static const date::year_month_day ole_zero( date::year(1899), date::month(12u), date::day(30u) );
    
    double intpart;
    double fractpart = modf( ole_dt, &intpart );
    
    const double max_long = static_cast<double>( numeric_limits<date::sys_days::rep>::max() );
    
    if( fabs(intpart) > max_long )
      return (intpart > 0.0) ? SpecUtils::time_point_t::max() : SpecUtils::time_point_t::min();
    
    const date::days::rep days = static_cast<date::days::rep>( intpart );
    
    SpecUtils::time_point_t pt = static_cast<date::sys_days>( ole_zero );
    pt += date::days(days);
    
    const auto nmilli = date::round<chrono::milliseconds>( chrono::duration<double, std::milli>(24 * 60 * 60 * 1000 * fractpart) );
    if( ole_dt >= 0.0 )
      pt += nmilli;
    else
      pt -= nmilli;
    
    return pt;
  }//SpecUtils::time_point_t datetime_ole_to_posix(double ole_dt)
}//namespace


namespace SpecUtils
{
  
bool SpecFile::load_ortec_listmode_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_ortec_listmode( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}
  
bool SpecFile::load_from_ortec_listmode( std::istream &input )
{
  if( !input.good() )
    return false;
  
  const streampos orig_pos = input.tellg();
  
  try
  {
    //http://www.ortec-online.com/download/List-Mode-File-Formats.pdf
    
    //For reference:
    //  2^21 = 2097152  (e.g. event us clock overflows every 2.097 seconds)
    //  2^31 = 2147483648
    //  2^32 = 4294967296
    //  2147.483647s = 35.79m (e.g. 31 bit clock us overflows every ~36 minutes)
    //  2^30 = 1073741824
    //  1073741.824 = 298 hours (e.g. digiBASE-E ms overflows every ~12 days, can ignore)
    
    double olestartdate;
    uint8_t energy_cal_valid, shape_cal_valid;
    float realtime, livetime;
    float offset, gain, quadratic;
    float shapeoffset, shapegain, shapequad;
    int32_t magicnum, lmstyle, conversiongain, detectoridint;
    char devaddress[81] = {'\0'}, mcb_type[10] = {'\0'}, energyunits[5] = {'\0'};
    char serialnumstr[17] = {'\0'}, txtdcreption[81] = {'\0'}, dummy[9];
    
    
    if( !input.read( (char *)&magicnum, 4) )
      throw runtime_error( "" );  //Failed to read from input stream

#if( !SpecUtils_BUILD_FUZZING_TESTS )
    if( magicnum != -13 )
      throw runtime_error( "Incorrect leading 4 bytes for .LIS file" );
#endif
    
    if( !input.read( (char *)&lmstyle, 4) )
      throw runtime_error( "" );  //Failed to read from input stream
    
    if( lmstyle != 1 && lmstyle != 2 && lmstyle != 4 )
      throw runtime_error( "Unrecognized listmode format" );
    
    if( lmstyle != 1 && lmstyle != 4 )
      throw runtime_error( "Listmode data not in digiBASE/digiBASE-E format (PRO List not supported yet)" );
    
    if( !input.read( (char *)&olestartdate, 8) )
      throw runtime_error( "" );
    
    if( !input.read( devaddress, 80) || !input.read( mcb_type, 9)
       || !input.read( serialnumstr, 16) || !input.read( txtdcreption, 80) )
      throw runtime_error( "" );
    
    if( !input.read( (char *)&energy_cal_valid, 1 ) || !input.read(energyunits,4)
       || !input.read( (char *)&offset, 4 ) || !input.read( (char *)&gain, 4 )
       || !input.read( (char *)&quadratic, 4 ) )
      throw runtime_error( "" );  //Failed reading energy calibration
    
    if( !input.read( (char *)&shape_cal_valid, 1 )
       || !input.read( (char *)&shapeoffset, 4 )
       || !input.read( (char *)&shapegain, 4 )
       || !input.read( (char *)&shapequad, 4 ) )
      throw runtime_error( "" ); //Failed reading shape calibration coefficents
    
    if( !input.read( (char *)&conversiongain, 4) )
      throw runtime_error( "" );
    
    if( !input.read( (char *)&detectoridint, 4) )
      throw runtime_error( "" );
    
    if( !input.read( (char *)&realtime, 4) || !input.read( (char *)&livetime, 4) )
      throw runtime_error( "" );
    
    if( !input.read(dummy, 9) )
      throw runtime_error( "" );
    
    assert( input.tellg() == (orig_pos + streampos(256)) );
    
    size_t ninitialbin = 1024;
    switch( lmstyle )
    {
      case 1: ninitialbin = 1024; break;
      case 2: ninitialbin = 1024; break;  //16k?
      case 4: ninitialbin = 2048; break;
    }
    
    auto histogram = std::make_shared< vector<float> >( ninitialbin, 0.0f );
    
    uint32_t event;
    
    if( lmstyle == 1 )
    {
      //We need to track overflows in the 31bit microsecond counter, so we will
      //  check if the current 31bit timestamp is less than the previous, and if
      //  so know a overflow occured, and add 2^31 to timeepoch.
      uint32_t previous_time = 0;
      
      //Incase real time isnt excplicitly given in the header we will grab the
      //  first and last events timetampts
      uint64_t firsttimestamp = 0, lasttimestamp = 0;
      
      //To track measurements longer than 35.79 minutes we need to keep track of
      //  more than a 31 bit clock.
      uint64_t timeepoch = 0;
      
      //Bits 20 through 31 of the timestamp (e.g. the part not given with actual
      //  hits)
      uint32_t time_msb = 0;
      
      //It appears that events with a zero value of the 21 bit timestamp will be
      //  transmitted before the 31 bit clock update (it will be the next 32bit
      //  event), so we have to add 2^21 to these zero clock value hits - however
      //  since these events are rare to investigate, I'm adding in an additional
      //  check to make sure that the 31 bit clock wasnt just sent, since I cant be
      //  sure the ordering is always consistent.
      bool prev_was_timestamp = false;
      
      //First, look for timestamp.  If first two timestamps most significant bits
      //  (20 though 31) are different, then the data is starting with the time_msb
      //  first given in file.  If first two timestamps are the same, then begging
      //  timestamp is this value minus 2^21.
      uint32_t firsttimestamps[2] = { 0 };
      for( int i = 0; i < 2 && input.read((char *)&event, 4); )
      {
        if( event > 0x7fffffff )
          firsttimestamps[i++] = (event & 0x7fe00000);
      }
      
      if( firsttimestamps[0] && firsttimestamps[0] == firsttimestamps[1] )
        time_msb = firsttimestamps[0] - 2097152;
      else
        time_msb = firsttimestamps[0];
      
      if( !input.seekg( orig_pos + streampos(256) ) )
        throw runtime_error( "" );
      
      for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      {
        if( event <= 0x7fffffff )
        {
          //Bits   Description
          //31     0 for event
          //30-21  Amplitude of pulse (10 bits)
          //20-0   Time in microseconds that the event occured (21 bits)
          
          //10-bit ADC value
          uint32_t amplitude = (uint32_t) ((event & 0x7fe00000) >> 21);
          
          //21-bit timestamp
          uint32_t time_lsb = (event & 0x001fffff);
          //Correct for time_lsb with value zero
          time_lsb = ((time_lsb || prev_was_timestamp) ? time_lsb : 2097152);
          const uint64_t timestamp = timeepoch + time_msb + time_lsb;
          
          if( amplitude > 16384 )
            throw runtime_error( "To high of a channel number" );
          
          amplitude = (amplitude > 0 ? amplitude-1 : amplitude);
          
          if( amplitude >= histogram->size() )
          {
            const uint32_t powexp = static_cast<uint32_t>( std::ceil(log(amplitude)/log(2)) );
            const size_t next_power_of_two = static_cast<size_t>( std::pow( 2u, powexp ) );
            histogram->resize( next_power_of_two, 0.0f );
          }
          
          ++((*histogram)[amplitude]);
          
          firsttimestamp = (firsttimestamp ? firsttimestamp : timestamp);
          lasttimestamp = timestamp;
          
          prev_was_timestamp = false;
        }else
        {
          //  The  number  rolls  over  to  0  every 2.097152 seconds. In  order  to  track
          //  the rollovers, a “time only” event is sent from the digiBASE to the
          //  computer every 1.048576 seconds.
          
          //Bits   Description
          //31     1 for time
          //30-0   Current time in microseconds
          const uint32_t this_time = (uint32_t) (event & 0x7fffffff);
          
          if( this_time < previous_time )
            timeepoch += 2147483648u;
          previous_time = this_time;
          time_msb = (this_time & 0xffe00000);
          prev_was_timestamp = true;
        }//if( a hit ) / else( a timestamp )
      }//for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      
      
      if( realtime == 0.0f )
        realtime = 1.0E-6f*(lasttimestamp - firsttimestamp);
      if( livetime == 0.0f )
        livetime = 1.0E-6f*(lasttimestamp - firsttimestamp);
    }else if( lmstyle == 4 )
    {
      uint32_t firstlivetimes[2] = { 0 }, firstrealtimes[2] = { 0 };
      
      if( !input.good() )
        throw runtime_error("");
      
      for( int i = 0, j = 0, k = 0; (i < 2 && j < 2) && input.read((char *)&event, 4); ++k)
      {
        const bool msb = (event & 0x80000000);
        const bool ssb = (event & 0x40000000);
        
        if( msb && ssb )
        {
          //ADC Word
        }else if( msb )
        {
          //RT Word, 30-bit Real Time counter in 10 mS Ticks
          firstrealtimes[i++] = (event & 0x3FFFFFFF);
        }else if( ssb )
        {
          //LT Word, 30 Bit Live Time counter in 10 mS Ticks
          firstlivetimes[j++] = (event & 0x3FFFFFFF);
        }else
        {
          //Ext Sync, 13-bit Real Time counter in 10 mS Ticks, 17-bit time pre-scale in 80 nS ticks
        }
      }
      
      size_t num_out_of_order = 0, num_events = 0;
      uint64_t firsttimestamp = 0, lasttimestamp = 0;
      uint64_t realtime_ns = 10*1000000 * uint64_t(firstrealtimes[0]);
      uint64_t livetime_ns = 10*1000000 * uint64_t(firstlivetimes[0]);
      
      //if( firsttimestamps[0] && firsttimestamps[0] == firsttimestamps[1] )
      //time_msb = firsttimestamps[0] - 2097152;
      //else
      //time_msb = firsttimestamps[0];
      
      
      if( !input.seekg( orig_pos + streampos(256) ) )
        throw runtime_error( "" );
      
      for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      {
        //Untested!
        if( (event & 0xC0000000) == 0xC0000000 )
        {
          //From DIGIBASE-E-MNL.pdf:
          //  Data[31:30] = 3
          //  Data[29] = Memory Routing bit
          //  Data[28:17] = Conversion Data[11:0]
          //  Data[16:0] = RealTime PreScale [17:1]
          
          //ADC Word: 17-bit time pre-scale in 80 nS Ticks, 13-bit ADC channel number
          //          const uint32_t adc_value = (event & 0x1FFF);
          const uint32_t ticks = (event & 0x0001FFFF);
          const uint64_t timestamp_ns = realtime_ns + 80*ticks;
          
          firsttimestamp = (!firsttimestamp ? timestamp_ns : firsttimestamp);
          
          num_events += 1;
          num_out_of_order += (timestamp_ns < lasttimestamp);
          
          lasttimestamp = timestamp_ns;
          
          
          //Have 2048 channels
          uint32_t amplitude = (uint32_t(event & 0x0FFE0000) >> 17);
          
          if( amplitude > 16384 )  // I dont think this will ever trigger
            throw runtime_error( "To high of a channel number" );
          
          if( amplitude >= histogram->size() )
          {
            const uint32_t powexp = static_cast<uint32_t>( std::ceil(log(amplitude)/log(2.0)) );
            const size_t next_power_of_two = static_cast<size_t>( std::pow( 2u, powexp ) );
            histogram->resize( next_power_of_two, 0.0f );
          }
          
          ++((*histogram)[amplitude]);
        }else if( (event & 0x80000000) == 0x80000000 )
        {
          //RT Word: 30-bit Real Time counter in 10 mS Ticks
          realtime_ns = 10*1000000*uint64_t(event & 0x3FFFFFFF);
        }else if( (event & 0x40000000) == 0x40000000 )
        {
          //LT Word: 30 Bit Live Time counter in 10 mS Ticks
          livetime_ns = 10*1000000*uint64_t(event & 0x3FFFFFFF);
        }else if( ((event ^ 0xC0000000) >> 30) == 0x00000003 )
        {
          /*
           //Ext Sync: 13-bit Real Time counter in 10 mS Ticks; 17-bit time pre-scale in 80 nS ticks
           //The Ext Sync words contain the value of the external input pulse counters.
           // The external pulse counters count the positive pulses at the external
           // input of the spectrometer. The sync time is calculated by adding the
           //  real time counter to the time pre-scale value.
           const uint32_t tenms_ticks = (uint32_t(event & 0x3FFE0000) >> 17);
           const uint32_t ns_ticks = (event & 0x1FFFF);
           
           
           from: https://oaktrust.library.tamu.edu/bitstream/handle/1969.1/ETD-TAMU-2012-08-11450/CraneWowUserManual.pdf?sequence=3&isAllowed=y
           Care must be taken as the real time
           portion of the stamp, found at bits 12-0, will reset every 80 stamps. This means that the sync time
           stamps roll over every 8 seconds. Note that the time stamp is in units of 100ms, such that a reading of
           “10” would equal one second. The remaining bits are a prescale in the units of 80ns that can be used to
           pinpoint the sync pulse in relation to the digiBASE-Es own clock. See the Sync Gating section of
           Explanations of Terms for more details.
           */
        }
      }//for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      
      if( num_events == 0 )
        throw runtime_error( "No events detected" );
      
      // If more than 1% of events are out of order, reject this as a valid file.  The 1% figure
      //  is not based on anything.
      if( (num_out_of_order > 2) && (num_out_of_order > (num_events/100)) )
        throw runtime_error( "Too many out-of-order listmode events" );
      
      
      //realtime=33.02, from hits->32.62
      //livetime=33.02, from hits->32.2
      //cout << "realtime=" << realtime << ", from hits->" << (1.0E-9*(lasttimestamp - firsttimestamp)) << endl;
      //cout << "livetime=" << realtime << ", from hits->" << (1.0E-9*(livetime_ns - (10*1000000 * uint64_t(firstlivetimes[0])))) << endl;
      
      if( realtime == 0.0f ) //not exact, but close enough
        realtime = 1.0E-9f*(lasttimestamp - firsttimestamp);
      
      if( livetime == 0.0 ) //get us within ~20ms live time calc, close enough!
        livetime = 1.0E-9f*(livetime_ns - (10*1000000 * uint64_t(firstlivetimes[0])));
    }else if( lmstyle == 2 )
    {
      throw runtime_error( "Unsupported listmode type 2" );
      //This one is pretty complicated, so I would definitely need some example
      //  data to test against.
    }else
    {
      throw runtime_error( "Unsupported listmode type " + std::to_string(lmstyle) );
    }//if( lmstyle == 1 ) / else
    
    
    
    const double gammasum = std::accumulate( histogram->begin(), histogram->end(), 0.0, std::plus<double>() );
    if( gammasum < 1.0 && realtime == 0.0f )
      throw runtime_error( "" ); //"Empty listmode file"
    
    std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
    
    meas->live_time_ = livetime;
    meas->real_time_ = realtime;
    meas->contained_neutron_ = false;
    meas->sample_number_ = 1;
    meas->occupied_ = OccupancyStatus::Unknown;
    meas->gamma_count_sum_ = gammasum;
    meas->neutron_counts_sum_ = 0.0;
    meas->detector_name_ = ((lmstyle==1) ? "digiBASE" : "digiBASE-E");
    meas->detector_number_ = 0;
    meas->detector_description_ = meas->detector_name_ + " ListMode data";
    meas->quality_status_ = SpecUtils::QualityStatus::Missing;
    meas->source_type_ = SourceType::Unknown;
    
    if( energy_cal_valid && (gain != 0.0f || quadratic != 0.0f) )
    {
      try
      {
        auto cal = make_shared<EnergyCalibration>();
        cal->set_polynomial( histogram->size(), {offset,gain,quadratic}, {} );
        meas->energy_calibration_ = cal;
      }catch( std::exception & )
      {
        meas->parse_warnings_.push_back( "Energy calibration given in file of polynomial {"
                                         + std::to_string(offset) + ", "
                                         + std::to_string(gain) + ", "
                                         + std::to_string(quadratic) + "}, was invalid." );
      }
    }//if( energy calibration should be valid )
  
    //std::vector<std::string>  remarks_;
    
    if( olestartdate > 0 )
      meas->start_time_ = datetime_ole_to_posix( olestartdate );
    
    meas->gamma_counts_ = histogram;
    //meas->neutron_counts_ = ...;
    //meas->latitude_;  //set to -999.9 if not specified
    //meas->longitude_; //set to -999.9 if not specified
    //meas->position_time_;
    
    
    meas->title_ = txtdcreption;
    
    instrument_type_ = "Spectroscopic Personal Radiation Detector";
    manufacturer_ = "ORTEC";
    instrument_model_ = ((lmstyle==1) ? "digiBASE" : "digiBASE-E");
    instrument_id_ = serialnumstr;
    if( instrument_id_.empty() && detectoridint )
    {
      char buffer[32];
      snprintf( buffer, sizeof(buffer), "%i", int(detectoridint) );
      instrument_id_ = buffer;
    }
    
    if( strlen(txtdcreption) )
      meas->measurement_description_ = txtdcreption;
    if( strlen(devaddress) )
      remarks_.push_back( "Device Address: " + string(devaddress) );
    if( strlen(mcb_type) )
      remarks_.push_back( "MCB Type: " + string(mcb_type) );
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    if( measurements_.empty() )
      throw std::runtime_error( "no measurements" );
    
#if( SpecUtils_BUILD_FUZZING_TESTS )
    if( magicnum != -13 )
      throw runtime_error( "Incorrect leading 4 bytes for .LIS file" );
#endif
    
    return true;
  }catch( std::exception &e )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }
  
  return false;
}//bool load_from_ortec_listmode( std::istream &input )

}//namespace SpecUtils




