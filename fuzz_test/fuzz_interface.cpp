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

#include <stdint.h>
#include <stddef.h>

#include <cstdint>
#include <sstream>

#include "SpecUtils/SpecFile.h"

#include "fuzz_interface.h"

using namespace std;


/**
TODO: If a file opens successfully, go through and try all the different accessors and modifiers.
TODO: when testin writing output, try different combinations of sample and detector numbers.
*/

void test_write_output( const SpecUtils::SpecFile &spec )
{
  using namespace SpecUtils;
  
  const set<int> &sample_numbers = spec.sample_numbers();
  const vector<int> &detector_numbers = spec.detector_numbers();
  const set<int> detnums( begin(detector_numbers), end(detector_numbers) );
  //const vector<string> &detector_names = spec.detector_names();

  if( sample_numbers.empty() || detector_numbers.empty() || !spec.num_gamma_channels() )
    return;

  for( SaveSpectrumAsType type = SaveSpectrumAsType(0); 
       type != SaveSpectrumAsType::NumTypes; 
       type = SaveSpectrumAsType(static_cast<int>(type) + 1) )
  {
    try
    {
      stringstream strm;
      spec.write( strm, sample_numbers, detnums, type );
    }catch( std::exception & )
    {

    }
    // TODO: try different combinations of sample and detector numbers...
/*
    switch( type )
    {
      case SaveSpectrumAsType::Txt:
      case SaveSpectrumAsType::Csv:
      case SaveSpectrumAsType::Pcf:
      case SaveSpectrumAsType::N42_2006:
      case SaveSpectrumAsType::N42_2012:
      case SaveSpectrumAsType::Chn:
      case SaveSpectrumAsType::SpcBinaryInt:
      case SaveSpectrumAsType::SpcBinaryFloat:
      case SaveSpectrumAsType::SpcAscii:
      case SaveSpectrumAsType::ExploraniumGr130v0:
      case SaveSpectrumAsType::ExploraniumGr135v2:
      case SaveSpectrumAsType::SpeIaea:
      case SaveSpectrumAsType::Cnf:
      case SaveSpectrumAsType::Tka:
        break;
  
#if( SpecUtils_ENABLE_D3_CHART )
      case SaveSpectrumAsType::HtmlD3:
#endif
  
#if( SpecUtils_INJA_TEMPLATES )
      case SaveSpectrumAsType::Template:
#endif
  
  case SaveSpectrumAsType::NumTypes
  break;
    };//switch( type )
*/
  }//for( loop over SaveSpectrumAsType )

}//void test_write_output( SpecUtils::SpecFile &spec )



int run_file_parse_fuzz( const uint8_t *data, size_t size ) 
{
  const string datastr( (const char *)data, size );
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in | ios_base::in );
    if( spec.load_from_N42( strm ) )
      test_write_output( spec );
  }
  
  if( size > 0 )
  {
    SpecUtils::SpecFile spec;
    string thisdatastr( (const char *)data, size );
    if( spec.load_N42_from_data( &(thisdatastr[0]), &(thisdatastr[0]) + size ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_iaea_spc( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_binary_spc( strm ) )
      test_write_output( spec );
  }
  
  if( size > 0 )
  {
    SpecUtils::SpecFile spec;
    string thisdatastr = datastr;
    if( spec.load_from_micro_raider_from_data( &thisdatastr[0] ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_binary_exploranium( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_pcf( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_txt_or_csv( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_Gr135_txt( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_spectroscopic_daily_file( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_srpm210_csv( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_amptek_mca( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_ortec_listmode( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_lsrm_spe( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_tka( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_multiact( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_phd( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_lzs( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_iaea( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_chn( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_cnf( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_tracs_mps( strm ) )
      test_write_output( spec );
  }
  
  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    if( spec.load_from_aram( strm ) )
      test_write_output( spec );
  }
  
  
  /*
   for( SpecUtils::ParserType parser_type = SpecUtils::ParserType(0);
   static_cast<int>(parser_type) < static_cast<int>(SpecUtils::ParserType::Auto);
   parser_type = SpecUtils::ParserType(static_cast<int>(parser_type) + 1 ) )
   {
   
   
   switch( parser_type )
   {
   case ParserType::N42_2006:
   case ParserType::N42_2012:
   success = spec.load_N42_from_data( data, data + size );
   break;
   
   case ParserType::Spc:
   success = load_spc_file( filename );
   break;
   
   case ParserType::Exploranium:
   success = load_binary_exploranium_file( filename );
   break;
   
   case ParserType::Pcf:
   success = load_pcf_file( filename );
   break;
   
   case ParserType::Chn:
   success = load_chn_file( filename );
   break;
   
   case ParserType::SpeIaea:
   success = load_iaea_file( filename );
   break;
   
   case ParserType::TxtOrCsv:
   success = load_txt_or_csv_file( filename );
   break;
   
   case ParserType::Cnf:
   success = load_cnf_file( filename );
   break;
   
   case ParserType::TracsMps:
   success = load_tracs_mps_file( filename );
   break;
   
   case ParserType::Aram:
   success = load_aram_file( filename );
   break;
   
   case ParserType::SPMDailyFile:
   success = load_spectroscopic_daily_file( filename );
   break;
   
   case ParserType::AmptekMca:
   success = load_amptek_file( filename );
   break;
   
   case ParserType::OrtecListMode:
   success = load_ortec_listmode_file( filename );
   break;
   
   case ParserType::LsrmSpe:
   success = load_lsrm_spe_file( filename );
   break;
   
   case ParserType::Tka:
   success = load_tka_file( filename );
   break;
   
   case ParserType::MultiAct:
   success = load_multiact_file( filename );
   break;
   
   case ParserType::Phd:
   success = load_phd_file( filename );
   break;
   
   case ParserType::Lzs:
   success = load_lzs_file( filename );
   break;
   
   case ParserType::MicroRaider:
   success = load_micro_raider_file( filename );
   break;
   
   case ParserType::Auto:
   break;
   };//switch( parser_type )
   */
  
  return 0;
}
