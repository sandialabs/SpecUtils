// spec_utils_fuzzer.cpp
#include <stdint.h>
#include <stddef.h>

#include <cstdint>
#include <sstream>

#include "SpecUtils/SpecFile.h"

using namespace std;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size ) 
{
  /*
  if (size > 0 && data[0] == 'H')
    if (size > 1 && data[1] == 'I')
       if (size > 2 && data[2] == '!')
       __builtin_trap();
  */

  const string datastr( (const char *)data, size );

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in | ios_base::in );
    spec.load_from_N42( strm );
  }

  if( size > 0 )
  {
    SpecUtils::SpecFile spec;
    string thisdatastr( (const char *)data, size );
    spec.load_N42_from_data( &(thisdatastr[0]), &(thisdatastr[0]) + size );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_iaea_spc( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_binary_spc( strm );
  }

  if( size > 0 )
  {
    SpecUtils::SpecFile spec;
    string thisdatastr = datastr;
    spec.load_from_micro_raider_from_data( &thisdatastr[0] );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_binary_exploranium( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_pcf( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_txt_or_csv( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_Gr135_txt( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_spectroscopic_daily_file( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_srpm210_csv( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_amptek_mca( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_ortec_listmode( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_lsrm_spe( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_tka( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_multiact( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_phd( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_lzs( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_iaea( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_chn( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_cnf( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_tracs_mps( strm );
  }

  {
    SpecUtils::SpecFile spec;
    stringstream strm( datastr, ios_base::in );
    spec.load_from_aram( strm );
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
