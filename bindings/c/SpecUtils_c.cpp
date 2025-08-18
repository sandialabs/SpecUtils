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

#include <chrono>
#include <cstring>
#include <fstream>
#include <assert.h>
#include <iostream>
#include <iterator>

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

#include "bindings/c/SpecUtils_c.h"

using namespace std;

// Begin some sanity checks to force us to keep the C interface in sync with the C++ interface
//
//  Check `SpecUtils_ParserType` has same values as `SpecUtils::ParserType enum`
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_N42_2006) == static_cast<int>(SpecUtils::ParserType::N42_2006),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_N42_2012) == static_cast<int>(SpecUtils::ParserType::N42_2012),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Spc) == static_cast<int>(SpecUtils::ParserType::Spc),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Exploranium) == static_cast<int>(SpecUtils::ParserType::Exploranium),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Pcf) == static_cast<int>(SpecUtils::ParserType::Pcf),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Chn) == static_cast<int>(SpecUtils::ParserType::Chn),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_SpeIaea) == static_cast<int>(SpecUtils::ParserType::SpeIaea),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_TxtOrCsv) == static_cast<int>(SpecUtils::ParserType::TxtOrCsv),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Cnf) == static_cast<int>(SpecUtils::ParserType::Cnf),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_TracsMps) == static_cast<int>(SpecUtils::ParserType::TracsMps),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Aram) == static_cast<int>(SpecUtils::ParserType::Aram),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_SPMDailyFile) == static_cast<int>(SpecUtils::ParserType::SPMDailyFile),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_AmptekMca) == static_cast<int>(SpecUtils::ParserType::AmptekMca),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_MicroRaider) == static_cast<int>(SpecUtils::ParserType::MicroRaider),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_RadiaCode) == static_cast<int>(SpecUtils::ParserType::RadiaCode),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_OrtecListMode) == static_cast<int>(SpecUtils::ParserType::OrtecListMode),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_LsrmSpe) == static_cast<int>(SpecUtils::ParserType::LsrmSpe),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Tka) == static_cast<int>(SpecUtils::ParserType::Tka),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_MultiAct) == static_cast<int>(SpecUtils::ParserType::MultiAct),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Phd) == static_cast<int>(SpecUtils::ParserType::Phd),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Lzs) == static_cast<int>(SpecUtils::ParserType::Lzs),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_ScanDataXml) == static_cast<int>(SpecUtils::ParserType::ScanDataXml),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Json) == static_cast<int>(SpecUtils::ParserType::Json),
              "SpecUtils_ParserType needs updating" );
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_CaenHexagonGXml) == static_cast<int>(SpecUtils::ParserType::CaenHexagonGXml),
              "SpecUtils_ParserType needs updating" );
#if( SpecUtils_ENABLE_URI_SPECTRA )
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Uri) == static_cast<int>(SpecUtils::ParserType::Uri),
              "SpecUtils_ParserType needs updating" );
#endif
static_assert( static_cast<int>(SpecUtils_ParserType::SpecUtils_Parser_Auto) == static_cast<int>(SpecUtils::ParserType::Auto),
              "SpecUtils_ParserType needs updating" );

  
// Check `SpecUtils_SaveSpectrumAsType` has same values as `SpecUtils::SaveSpectrumAsType`.
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsTxt)
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Txt),
              "SpecUtils_SaveSpectrumAsType needs updating" );

static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsCsv) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Csv),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsPcf) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Pcf),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsN42_2006) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::N42_2006),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsN42_2012) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::N42_2012),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsChn) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Chn),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsSpcBinaryInt) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::SpcBinaryInt),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsSpcBinaryFloat) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::SpcBinaryFloat),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsSpcAscii) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::SpcAscii),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsExploraniumGr130v0) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::ExploraniumGr130v0),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsExploraniumGr135v2) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::ExploraniumGr135v2),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsSpeIaea) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::SpeIaea),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsCnf) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Cnf),
              "SpecUtils_SaveSpectrumAsType needs updating" );
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsTka) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Tka),
              "SpecUtils_SaveSpectrumAsType needs updating" );

#if( SpecUtils_ENABLE_D3_CHART )
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsHtmlD3) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::HtmlD3),
              "SpecUtils_SaveSpectrumAsType needs updating" );
#endif

#if( SpecUtils_INJA_TEMPLATES )
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsTemplate) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Template),
              "SpecUtils_SaveSpectrumAsType needs updating" );
#endif

#if( SpecUtils_ENABLE_URI_SPECTRA )
static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsUri) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::Uri),
              "SpecUtils_SaveSpectrumAsType needs updating" );
#endif

static_assert( static_cast<int>(SpecUtils_SaveSpectrumAsType::SpecUtils_SaveAsNumTypes) 
              == static_cast<int>(SpecUtils::SaveSpectrumAsType::NumTypes),
              "SpecUtils_SaveSpectrumAsType needs updating" );


// Check `SpecUtils_DetectorType` has same values as `SpecUtils::DetectorType`.
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Exploranium)
                == static_cast<int>(SpecUtils::DetectorType::Exploranium),
                "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinder)
                == static_cast<int>(SpecUtils::DetectorType::IdentiFinder),
                "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderNG)
                == static_cast<int>(SpecUtils::DetectorType::IdentiFinderNG),
                "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderLaBr3)
                == static_cast<int>(SpecUtils::DetectorType::IdentiFinderLaBr3),
                "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderTungsten)
                == static_cast<int>(SpecUtils::DetectorType::IdentiFinderTungsten),
                "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderR425NaI)
                == static_cast<int>(SpecUtils::DetectorType::IdentiFinderR425NaI),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderR425LaBr)
              == static_cast<int>(SpecUtils::DetectorType::IdentiFinderR425LaBr),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderR500NaI) 
              == static_cast<int>(SpecUtils::DetectorType::IdentiFinderR500NaI),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderR500LaBr) 
              == static_cast<int>(SpecUtils::DetectorType::IdentiFinderR500LaBr),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_IdentiFinderUnknown) 
              == static_cast<int>(SpecUtils::DetectorType::IdentiFinderUnknown),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_DetectiveUnknown) 
              == static_cast<int>(SpecUtils::DetectorType::DetectiveUnknown),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_DetectiveEx) 
              == static_cast<int>(SpecUtils::DetectorType::DetectiveEx),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_DetectiveEx100) 
              == static_cast<int>(SpecUtils::DetectorType::DetectiveEx100),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_DetectiveEx200) 
              == static_cast<int>(SpecUtils::DetectorType::DetectiveEx200),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_DetectiveX) 
              == static_cast<int>(SpecUtils::DetectorType::DetectiveX),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_SAIC8) 
              == static_cast<int>(SpecUtils::DetectorType::SAIC8),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Falcon5000) 
              == static_cast<int>(SpecUtils::DetectorType::Falcon5000),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_MicroDetective) 
              == static_cast<int>(SpecUtils::DetectorType::MicroDetective),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_MicroRaider) 
              == static_cast<int>(SpecUtils::DetectorType::MicroRaider),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RadiaCodeCsI10)
              == static_cast<int>(SpecUtils::DetectorType::RadiaCodeCsI10),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Raysid)
              == static_cast<int>(SpecUtils::DetectorType::Raysid),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Interceptor)
              == static_cast<int>(SpecUtils::DetectorType::Interceptor),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RadHunterNaI) 
              == static_cast<int>(SpecUtils::DetectorType::RadHunterNaI),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RadHunterLaBr3) 
              == static_cast<int>(SpecUtils::DetectorType::RadHunterLaBr3),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Rsi701) 
              == static_cast<int>(SpecUtils::DetectorType::Rsi701),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Rsi705) 
              == static_cast<int>(SpecUtils::DetectorType::Rsi705),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_AvidRsi) 
              == static_cast<int>(SpecUtils::DetectorType::AvidRsi),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_OrtecRadEagleNai) 
              == static_cast<int>(SpecUtils::DetectorType::OrtecRadEagleNai),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_OrtecRadEagleCeBr2Inch) 
              == static_cast<int>(SpecUtils::DetectorType::OrtecRadEagleCeBr2Inch),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_OrtecRadEagleCeBr3Inch)
              == static_cast<int>(SpecUtils::DetectorType::OrtecRadEagleCeBr3Inch),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_OrtecRadEagleLaBr) 
              == static_cast<int>(SpecUtils::DetectorType::OrtecRadEagleLaBr),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Sam940LaBr3) 
              == static_cast<int>(SpecUtils::DetectorType::Sam940LaBr3),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Sam940) 
              == static_cast<int>(SpecUtils::DetectorType::Sam940),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Sam945) 
              == static_cast<int>(SpecUtils::DetectorType::Sam945),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Srpm210)
              == static_cast<int>(SpecUtils::DetectorType::Srpm210),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RIIDEyeNaI)
              == static_cast<int>(SpecUtils::DetectorType::RIIDEyeNaI),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RIIDEyeLaBr)
              == static_cast<int>(SpecUtils::DetectorType::RIIDEyeLaBr),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RadSeekerNaI)
              == static_cast<int>(SpecUtils::DetectorType::RadSeekerNaI),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_RadSeekerLaBr)
              == static_cast<int>(SpecUtils::DetectorType::RadSeekerLaBr),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_VerifinderNaI)
              == static_cast<int>(SpecUtils::DetectorType::VerifinderNaI),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_VerifinderLaBr)
              == static_cast<int>(SpecUtils::DetectorType::VerifinderLaBr),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_KromekD3S)
              == static_cast<int>(SpecUtils::DetectorType::KromekD3S),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_KromekD5)
              == static_cast<int>(SpecUtils::DetectorType::KromekD5),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_KromekGR1)
              == static_cast<int>(SpecUtils::DetectorType::KromekGR1),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Fulcrum)
              == static_cast<int>(SpecUtils::DetectorType::Fulcrum),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Fulcrum40h)
              == static_cast<int>(SpecUtils::DetectorType::Fulcrum40h),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Sam950)
              == static_cast<int>(SpecUtils::DetectorType::Sam950),
              "SpecUtils_DetectorType needs updating" );
static_assert( static_cast<int>(SpecUtils_DetectorType::SpecUtils_Det_Unknown)
              == static_cast<int>(SpecUtils::DetectorType::Unknown),
              "SpecUtils_DetectorType needs updating" );
  

// Check `SpecUtils_SourceType` has same values as `SpecUtils::SourceType`.
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_IntrinsicActivity)
              == static_cast<int>(SpecUtils::SourceType::IntrinsicActivity),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Calibration)
              == static_cast<int>(SpecUtils::SourceType::Calibration),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Background)
              == static_cast<int>(SpecUtils::SourceType::Background),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Foreground)
              == static_cast<int>(SpecUtils::SourceType::Foreground),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Unknown)
              == static_cast<int>(SpecUtils::SourceType::Unknown),
              "SpecUtils_SourceType needs updating" );
  

// Check `SpecUtils_EnergyCalType` has same values as `SpecUtils::EnergyCalType`.
static_assert( static_cast<int>(SpecUtils_EnergyCal_Polynomial)
              == static_cast<int>(SpecUtils::EnergyCalType::Polynomial),
              "SpecUtils_EnergyCalType needs updating" );
static_assert( static_cast<int>(SpecUtils_EnergyCal_FullRangeFraction)
              == static_cast<int>(SpecUtils::EnergyCalType::FullRangeFraction),
              "SpecUtils_EnergyCalType needs updating" );
static_assert( static_cast<int>(SpecUtils_EnergyCal_LowerChannelEdge)
              == static_cast<int>(SpecUtils::EnergyCalType::LowerChannelEdge),
              "SpecUtils_EnergyCalType needs updating" );
static_assert( static_cast<int>(SpecUtils_EnergyCal_UnspecifiedUsingDefaultPolynomial)
              == static_cast<int>(SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial),
              "SpecUtils_EnergyCalType needs updating" );
static_assert( static_cast<int>(SpecUtils_EnergyCal_InvalidEquationType)
              == static_cast<int>(SpecUtils::EnergyCalType::InvalidEquationType),
              "SpecUtils_EnergyCalType needs updating" );


// Check `SpecUtils_OccupancyStatus` has same values as `SpecUtils::OccupancyStatus`.
static_assert( static_cast<int>(SpecUtils_OccupancyStatus::SpecUtils_OccupancyStatus_NotOccupied)
              == static_cast<int>(SpecUtils::OccupancyStatus::NotOccupied),
              "SpecUtils_OccupancyStatus needs updating" );
static_assert( static_cast<int>(SpecUtils_OccupancyStatus::SpecUtils_OccupancyStatus_Occupied)
              == static_cast<int>(SpecUtils::OccupancyStatus::Occupied),
              "SpecUtils_OccupancyStatus needs updating" );
static_assert( static_cast<int>(SpecUtils_OccupancyStatus::SpecUtils_OccupancyStatus_Unknown)
              == static_cast<int>(SpecUtils::OccupancyStatus::Unknown),
              "SpecUtils_OccupancyStatus needs updating" );


// Check `SpecUtils_QualityStatus_Good` has same values as `SpecUtils::QualityStatus`.
static_assert( static_cast<int>(SpecUtils_QualityStatus::SpecUtils_QualityStatus_Good)
              == static_cast<int>(SpecUtils::QualityStatus::Good),
              "SpecUtils_QualityStatus needs updating" );
static_assert( static_cast<int>(SpecUtils_QualityStatus::SpecUtils_QualityStatus_Suspect)
              == static_cast<int>(SpecUtils::QualityStatus::Suspect),
              "SpecUtils_QualityStatus needs updating" );
static_assert( static_cast<int>(SpecUtils_QualityStatus::SpecUtils_QualityStatus_Bad)
              == static_cast<int>(SpecUtils::QualityStatus::Bad),
              "SpecUtils_QualityStatus needs updating" );
static_assert( static_cast<int>(SpecUtils_QualityStatus::SpecUtils_QualityStatus_Missing)
              == static_cast<int>(SpecUtils::QualityStatus::Missing),
              "SpecUtils_QualityStatus needs updating" );

// Check `SpecUtils_SourceType` has same values as `SpecUtils::SourceType`.
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_IntrinsicActivity)
              == static_cast<int>(SpecUtils::SourceType::IntrinsicActivity),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Calibration)
              == static_cast<int>(SpecUtils::SourceType::Calibration),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Background)
              == static_cast<int>(SpecUtils::SourceType::Background),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Foreground)
              == static_cast<int>(SpecUtils::SourceType::Foreground),
              "SpecUtils_SourceType needs updating" );
static_assert( static_cast<int>(SpecUtils_SourceType::SpecUtils_SourceType_Unknown)
              == static_cast<int>(SpecUtils::SourceType::Unknown),
              "SpecUtils_SourceType needs updating" );



namespace // - private functions for this file
{
  shared_ptr<const SpecUtils::Measurement> get_shared_ptr( const SpecUtils::SpecFile * const specfile,
                                                               const SpecUtils_Measurement * const measurement )
  {
    if( !specfile || !measurement )
      return nullptr;
    
    const SpecUtils::Measurement * const meas = reinterpret_cast<const SpecUtils::Measurement *>( measurement );
    
    const int sample_num = meas->sample_number();
    const string &det_name = meas->detector_name();
    
    shared_ptr<const SpecUtils::Measurement> trial_meas = specfile->measurement( sample_num, det_name );
    if( trial_meas && (trial_meas.get() == meas) )
      return trial_meas;
    
    vector<shared_ptr<const SpecUtils::Measurement>> all_meas = specfile->measurements();
    for( const shared_ptr<const SpecUtils::Measurement> &m : all_meas )
    {
      if( m.get() == meas )
        return m;
    }
    
    assert( 0 );
    
    return nullptr;
  }//get_shared_ptr(...)
}//namespace - private functions for this file


SpecUtils_SpecFile *SpecUtils_SpecFile_create()
{
  SpecUtils::SpecFile *ptr = new SpecUtils::SpecFile();
  return reinterpret_cast<SpecUtils_SpecFile *>( ptr );
}


SpecUtils_SpecFile *SpecUtils_SpecFile_clone( const SpecUtils_SpecFile * const instance )
{
  try
  {
    const SpecUtils::SpecFile *orig = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
    if( !orig )
      throw runtime_error( "null input" );
    
    SpecUtils::SpecFile *copy = new SpecUtils::SpecFile();
    *copy = *orig;
    
    return reinterpret_cast<SpecUtils_SpecFile *>( copy );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_SpecFile_clone: " << e.what() << endl;
  }
  
  return nullptr;
}
  

void SpecUtils_SpecFile_destroy( SpecUtils_SpecFile *instance )
{
  assert( instance );
  SpecUtils::SpecFile *ptr = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  if( ptr )
    delete ptr;
}


void SpecUtils_SpecFile_set_equal( SpecUtils_SpecFile *lhs, const SpecUtils_SpecFile *rhs )
{
  try
  {
    auto lhs_raw = reinterpret_cast<SpecUtils::SpecFile *>( lhs );
    auto rhs_raw = reinterpret_cast<const SpecUtils::SpecFile *>( rhs );
    if( !lhs_raw )
      throw runtime_error( "null lhs" );
    
    if( !rhs_raw )
      throw runtime_error( "null lhs" );
    
    *lhs_raw = *rhs_raw;
  }catch( std::exception &e )
  {
    // Really dont expect to get here - so we arent returning an error code or anything
    cerr << "SpecUtils_SpecFile_set_equal: " << e.what() << endl;
  }
}


bool SpecFile_load_file( SpecUtils_SpecFile *instance, const char * const filename )
{
  assert( instance );
  if( !instance )
    return false;
  
  SpecUtils::SpecFile *ptr = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  const bool success = ptr->load_file( filename, SpecUtils::ParserType::Auto, filename );
  
  return success;
}


bool SpecFile_load_file_from_format( SpecUtils_SpecFile * const instance,
                                  const char * const filename,
                                  const SpecUtils_ParserType type )
{
  assert( instance );
  if( !instance )
    return false;
  
  SpecUtils::SpecFile *ptr = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  
  assert( (static_cast<int>(type) >= 0)
         && (static_cast<int>(type) <= static_cast<int>(SpecUtils::ParserType::Auto)) );
  
  const SpecUtils::ParserType spt = SpecUtils::ParserType( static_cast<int>(type) );
  
  const bool success = ptr->load_file( filename, spt, filename );
  
  return success;
}
  
  

bool SpecUtils_write_to_file( SpecUtils_SpecFile *instance,
                           const char *filename,
                           SpecUtils_SaveSpectrumAsType type )
{
  assert( instance && filename );
  if( !instance || !filename )
    return false;
  
  const SpecUtils::SaveSpectrumAsType save_type = SpecUtils::SaveSpectrumAsType( static_cast<int>(type) );
  SpecUtils::SpecFile *ptr = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  
  try
  {
    ptr->write_to_file( filename, save_type );
  }catch( std::exception &e )
  {
    std::cerr << "SpecUtils_write_to_file - failed write: " << e.what() << endl;
    return false;
  }
  
  return true;
}//
  


bool SpecUtils_SpecFile_passthrough( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr && ptr->passthrough();
}

  
uint32_t SpecUtils_SpecFile_number_measurements( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr ? static_cast<uint32_t>( ptr->num_measurements() ) : uint32_t(0);
}
  

uint32_t SpecUtils_SpecFile_number_gamma_channels( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr ? static_cast<uint32_t>( ptr->num_gamma_channels() ) : uint32_t(0);
}


const SpecUtils_Measurement *
SpecUtils_SpecFile_get_measurement_by_index( const SpecUtils_SpecFile * const instance,
                                     const uint32_t index )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  if( !ptr )
    return nullptr;
  std::shared_ptr<const SpecUtils::Measurement> meas = ptr->measurement( size_t(index) );
  return reinterpret_cast<const SpecUtils_Measurement *>( meas.get() );
}
  

const SpecUtils_Measurement *
SpecUtils_SpecFile_get_measurement_by_sample_det( const SpecUtils_SpecFile * const instance,
                                          const int sample_number,
                                          const char * const det_name )
{
  assert( instance && det_name );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  if( !ptr || !det_name )
    return nullptr;
  
  const string det_name_str = det_name;
  shared_ptr<const SpecUtils::Measurement> meas = ptr->measurement( sample_number, det_name_str );
  
  return reinterpret_cast<const SpecUtils_Measurement *>( meas.get() );
}
  

uint32_t SpecUtils_SpecFile_number_detectors( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr ? static_cast<uint32_t>(ptr->detector_names().size()) : uint32_t(0);
}


const char *SpecUtils_SpecFile_detector_name( const SpecUtils_SpecFile * const instance,
                                 const uint32_t index )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  if( !ptr )
    return nullptr;
  
  const vector<string> &names = ptr->detector_names();
  if( static_cast<size_t>(index) >= names.size() )
    return nullptr;
  
  return names[index].c_str();
}


uint32_t SpecUtils_SpecFile_number_gamma_detectors( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr ? static_cast<uint32_t>(ptr->gamma_detector_names().size()) : uint32_t(0);
}


DLLEXPORT const char * CALLINGCONVENTION
SpecUtils_SpecFile_gamma_detector_name( const SpecUtils_SpecFile * const instance,
                                   const uint32_t index )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  if( !ptr )
    return nullptr;
  
  const vector<string> &names = ptr->gamma_detector_names();
  if( static_cast<size_t>(index) >= names.size() )
    return nullptr;
  
  return names[index].c_str();
}


uint32_t SpecUtils_SpecFile_number_neutron_detectors( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr ? static_cast<uint32_t>(ptr->neutron_detector_names().size()) : uint32_t(0);
}


const char * SpecUtils_SpecFile_neutron_detector_name( const SpecUtils_SpecFile * const instance,
                                     const uint32_t index )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  if( !ptr )
    return nullptr;
  
  const vector<string> &names = ptr->neutron_detector_names();
  if( static_cast<size_t>(index) >= names.size() )
    return nullptr;
  
  return names[index].c_str();
}

  
uint32_t SpecUtils_SpecFile_number_samples( const SpecUtils_SpecFile * const instance )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  return ptr ? static_cast<uint32_t>(ptr->sample_numbers().size()) : uint32_t(0);
}
  
/** Returns the sample number, for a given index.
 
 @param instance The `SpecUtils_SpecFile` to access.
 @param index Inclusively between 0 and `SpecUtils_SpecFile_number_samples(instance) - 1`.
 @returns The sample number for the given index.  If index is invalid, returns `INT_MIN`
 */
DLLEXPORT int CALLINGCONVENTION
SpecUtils_SpecFile_sample_number( const SpecUtils_SpecFile * const instance, const uint32_t index )
{
  assert( instance );
  const SpecUtils::SpecFile *ptr = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  if( !ptr || (index >= static_cast<uint32_t>(ptr->sample_numbers().size())) )
    return std::numeric_limits<int>::min();
  
  const set<int> &samples = ptr->sample_numbers();
  auto it = begin(samples);
  std::advance(it, index);
  
  return *it;
}


bool SpecUtils_SpecFile_add_measurement( SpecUtils_SpecFile * instance,
                SpecUtils_Measurement *measurement,
                const bool do_cleanup )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  SpecUtils::Measurement *meas_raw = reinterpret_cast<SpecUtils::Measurement *>( measurement );
  assert( meas_raw && specfile );
  if( !meas_raw || !specfile )
    return false;
  
  specfile->add_measurement( shared_ptr<SpecUtils::Measurement>( meas_raw ), do_cleanup );
  
  return true;
}


bool SpecUtils_SpecFile_remove_measurement( SpecUtils_SpecFile * instance,
                   const SpecUtils_Measurement * const measurement,
                   const bool do_cleanup )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  
  shared_ptr<const SpecUtils::Measurement> owned_meas = get_shared_ptr( specfile, measurement );
  assert( owned_meas );
  if( !owned_meas )
    return false;
  
  specfile->remove_measurement( owned_meas, do_cleanup );
  
  return true;
}


bool SpecUtils_SpecFile_remove_measurements( SpecUtils_SpecFile * instance,
                    const SpecUtils_Measurement ** const measurements,
                    const uint32_t number_to_remove )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  if( !specfile || !measurements )
    return false;
  
  if( number_to_remove == 0 )
    return true;
   
  vector<shared_ptr<const SpecUtils::Measurement>> owned_meass( number_to_remove );
  for( uint32_t index = 0; index < number_to_remove; index += 1 )
  {
    owned_meass[index] = get_shared_ptr( specfile, measurements[index] );
    assert( owned_meass[index] );
    if( !owned_meass[index] )
      return false;
  }
  
  specfile->remove_measurements( owned_meass );
  
  return true;
}


void SpecUtils_SpecFile_reset( SpecUtils_SpecFile * instance )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  if( specfile )
    specfile->reset();
}


void SpecUtils_SpecFile_cleanup( SpecUtils_SpecFile * instance,
                                const bool dont_change_sample_numbers,
                                const bool reorder_by_time )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  unsigned int flags = SpecUtils::SpecFile::CleanupAfterLoadFlags::StandardCleanup;
  
  if( reorder_by_time )
    flags |= SpecUtils::SpecFile::CleanupAfterLoadFlags::ReorderSamplesByTime;
  
  if( dont_change_sample_numbers && !reorder_by_time )
    flags |= SpecUtils::SpecFile::CleanupAfterLoadFlags::DontChangeOrReorderSamples;
  
  if( specfile )
    specfile->cleanup_after_load( flags );
}


bool SpecUtils_SpecFile_modified( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->modified() : false;
}
  

SpecUtils_Measurement *
SpecUtils_SpecFile_sum_measurements( const SpecUtils_SpecFile * const instance,
                                    const int * const sample_numbers,
                                    const uint32_t number_sample_numbers,
                                    const char ** const detector_names,
                                    const uint32_t number_detector_names )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( !specfile )
    return nullptr;
  
  const set<int> samplenums( sample_numbers, sample_numbers + number_sample_numbers );
  const vector<string> det_names( detector_names, detector_names + number_detector_names );
  
  shared_ptr<SpecUtils::Measurement> result = specfile->sum_measurements( samplenums, det_names, nullptr);
  assert( result );
  if( !result )
    return nullptr;
  
  // I dont think there is a safe/legal way to release a pointer from a smart pointer???
  
  SpecUtils::Measurement *res_cpy = new SpecUtils::Measurement( *result );
  
  return reinterpret_cast<SpecUtils_Measurement *>( res_cpy );
}//


uint32_t SpecUtils_SpecFile_memmorysize( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return static_cast<uint32_t>( specfile ? specfile->memmorysize() : size_t(0) );
}
  

uint32_t SpecUtils_SpecFile_number_remarks( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return static_cast<uint32_t>( specfile ? specfile->remarks().size() : size_t(0) );
}


const char *SpecUtils_SpecFile_remark( const SpecUtils_SpecFile * const instance,
                          const uint32_t index )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile || (index >= specfile->remarks().size()) )
    return nullptr;
  
  const vector<string> &remarks = specfile->remarks();
  return remarks[index].c_str();
}
  

uint32_t SpecUtils_SpecFile_number_parse_warnings( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return static_cast<uint32_t>( specfile ? specfile->parse_warnings().size() : size_t(0) );
}


const char *SpecUtils_SpecFile_parse_warning( const SpecUtils_SpecFile * const instance,
                                 const uint32_t index  )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile || (index >= specfile->parse_warnings().size()) )
    return nullptr;
  
  const vector<string> &warnings = specfile->parse_warnings();
  return warnings[index].c_str();
}
  

float SpecUtils_SpecFile_sum_gamma_live_time( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->gamma_live_time() : 0.0f;
}


float SpecUtils_SpecFile_sum_gamma_real_time( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->gamma_real_time() : 0.0f;
}


double SpecUtils_SpecFile_gamma_count_sum( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->gamma_count_sum() : 0.0;
}


double SpecUtils_SpecFile_neutron_counts_sum( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->neutron_counts_sum() : 0.0;
}
  

const char *SpecUtils_SpecFile_filename( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &filename = specfile->filename();
  return filename.c_str();
}


const char *SpecUtils_SpecFile_uuid( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &uuid = specfile->uuid();
  return uuid.c_str();
}
  

const char *SpecUtils_SpecFile_measurement_location_name( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &location_name = specfile->measurement_location_name();
  return location_name.c_str();
}
    

const char *SpecUtils_SpecFile_measurement_operator( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &meas_operator = specfile->measurement_operator();
  return meas_operator.c_str();
}


SpecUtils_DetectorType SpecUtils_SpecFile_detector_type( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );

  if( !specfile )
    return SpecUtils_DetectorType::SpecUtils_Det_Unknown;
  
  return SpecUtils_DetectorType( static_cast<int>(specfile->detector_type()) );
}


const char *SpecUtils_SpecFile_instrument_type( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &instrument_type = specfile->instrument_type();
  return instrument_type.c_str();
}


const char *SpecUtils_SpecFile_manufacturer( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &manufacturer = specfile->manufacturer();
  return manufacturer.c_str();
}


const char *SpecUtils_SpecFile_instrument_model( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &instrument_model = specfile->instrument_model();
  return instrument_model.c_str();
}
  

const char *SpecUtils_SpecFile_instrument_id( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  if( !specfile )
    return nullptr;
  
  const string &instrument_id = specfile->instrument_id();
  return instrument_id.c_str();
}
  

bool SpecUtils_SpecFile_has_gps_info( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->has_gps_info() : false;
}
  

double SpecUtils_SpecFile_mean_latitude( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->mean_latitude() : -999.9;
}

  
double SpecUtils_SpecFile_mean_longitude( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->mean_longitude() : -999.9;
}
    

bool SpecUtils_SpecFile_contains_derived_data( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->contains_derived_data() : false;
}
  

bool SpecUtils_SpecFile_contains_non_derived_data( const SpecUtils_SpecFile * const instance )
{
  const SpecUtils::SpecFile *specfile = reinterpret_cast<const SpecUtils::SpecFile *>( instance );
  assert( specfile );
  return specfile ? specfile->contains_non_derived_data() : false;
}


void SpecUtils_SpecFile_set_filename( SpecUtils_SpecFile *instance,
                                  const char * const filename )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile && filename )
    specfile->set_filename( filename );
}


void SpecUtils_SpecFile_set_remarks( SpecUtils_SpecFile *instance,
                                 const char ** const remarks,
                                 const uint32_t number_remarks )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile && remarks );
  if( !specfile || !remarks )
    return;
  
  vector<string> remarksvec( number_remarks );
  for( uint32_t i = 0; i < number_remarks; ++i )
  {
    assert( remarks[i] );
    remarksvec[i] = remarks[i];
  }
  
  specfile->set_remarks( remarksvec );
}

  
void SpecUtils_SpecFile_add_remark( SpecUtils_SpecFile *instance,
                                const char * const remark )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile && remark );
  if( specfile && remark )
    specfile->add_remark( remark );
}

  
void SpecUtils_SpecFile_set_parse_warnings( SpecUtils_SpecFile *instance,
                                        const char ** const warnings,
                                        const uint32_t number_warnings )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile && warnings );
  if( !specfile || !warnings )
    return;
  
  vector<string> warningsvec( number_warnings );
  for( uint32_t i = 0; i < number_warnings; ++i )
  {
    assert( warnings[i] );
    warningsvec[i] = warnings[i];
  }
  
  specfile->set_parse_warnings( warningsvec );
}


void SpecUtils_SpecFile_set_uuid( SpecUtils_SpecFile *instance,
                              const char * const file_uuid )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_uuid( file_uuid ? file_uuid : "" );
}

  
void SpecUtils_SpecFile_set_lane_number( SpecUtils_SpecFile *instance, const int num )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_lane_number( num );
}
  
void SpecUtils_SpecFile_set_measurement_location_name( SpecUtils_SpecFile *instance,
                                                   const char * const location_name )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_measurement_location_name( location_name ? location_name : "" );
}
  

void SpecUtils_SpecFile_set_inspection( SpecUtils_SpecFile *instance,
                                    const char * const inspection_type )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_inspection( inspection_type ? inspection_type : "" );
}
  
  
void SpecUtils_SpecFile_set_instrument_type( SpecUtils_SpecFile *instance,
                                         const char * const instrument_type )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_instrument_type( instrument_type ? instrument_type : "" );
}

  
void SpecUtils_SpecFile_set_detector_type( SpecUtils_SpecFile *instance,
                                       const SpecUtils_DetectorType type )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( !specfile )
    return;
  
  const SpecUtils::DetectorType dettype = SpecUtils::DetectorType( static_cast<int>(type) );
  specfile->set_detector_type( dettype );
}

  
void SpecUtils_SpecFile_set_manufacturer( SpecUtils_SpecFile *instance,
                                      const char * const manufacturer_name )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_manufacturer( manufacturer_name ? manufacturer_name : "" );
}
  

void SpecUtils_SpecFile_set_instrument_model( SpecUtils_SpecFile *instance,
                                          const char * const model )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_instrument_model( model ? model : "" );
}
  

void SpecUtils_SpecFile_set_instrument_id( SpecUtils_SpecFile *instance,
                                       const char * const serial_number )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( specfile )
    specfile->set_instrument_id( serial_number ? serial_number : "" );
}


bool SpecUtils_SpecFile_change_detector_name( SpecUtils_SpecFile *instance,
                            const char * const original_name,
                            const char * const new_name )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  
  try
  {
    if( !specfile || !original_name || !new_name )
      throw runtime_error( "Invalid input pointer" );
    
    specfile->change_detector_name( original_name, new_name );
    
    return true;
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_SpecFile_change_detector_name: " << e.what() << endl;
    return false;
  }
  
  return false;
}


bool set_energy_calibration_from_CALp_file( SpecUtils_SpecFile *instance,
                                             const char * const CALp_filepath )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  assert( specfile );
  if( !specfile )
    return false;
  
#ifdef _WIN32
  const std::wstring wfilename = SpecUtils::convert_from_utf8_to_utf16(CALp_filepath);
  std::ifstream t( wfilename.c_str() );
#else
  std::ifstream t( CALp_filepath );
#endif
  
  try
  {
    if( !t )
      throw runtime_error( "couldnt open input CALp file." );
    
    specfile->set_energy_calibration_from_CALp_file( t );
    
    return true;
  }catch( std::exception &e )
  {
    cerr << "set_energy_calibration_from_CALp_file: " << e.what() << endl;
  }
  
  return false;
}


bool SpecUtils_SpecFile_set_measurement_live_time( SpecUtils_SpecFile *instance,
                                   const float live_time,
                                   const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  
  if( m )
    specfile->set_live_time( live_time, m );
  
  return !!m;
}


bool SpecUtils_SpecFile_set_measurement_real_time( SpecUtils_SpecFile *instance,
                                   const float real_time,
                                   const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  
  if( m )
    specfile->set_real_time( real_time, m );
  
  return !!m;
}
  

bool SpecUtils_SpecFile_set_measurement_start_time( SpecUtils_SpecFile *instance,
                                    const int64_t microseconds_since_unix_epoch,
                                    const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  
  SpecUtils::time_point_t tp{};
  tp += chrono::microseconds( microseconds_since_unix_epoch );
  
  if( m )
    specfile->set_start_time( tp, m );
  
  return !!m;
}


bool SpecUtils_SpecFile_set_measurement_start_time_str( SpecUtils_SpecFile *instance,
                                    const char *date_time,
                                    const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  if( !specfile || !date_time || !m )
    return false;
  
  const SpecUtils::time_point_t tp = SpecUtils::time_from_string( date_time );
  const bool valid_dt = SpecUtils::is_special(tp);
  
  if( m && valid_dt )
    specfile->set_start_time( tp, m );
  
  return (m && valid_dt);
}


bool SpecUtils_SpecFile_set_measurement_remarks( SpecUtils_SpecFile *instance,
                                 const char ** const remarks,
                                 const uint32_t number_remarks,
                                 const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  if( !specfile || !m )
    return false;
  
  vector<string> remarksvec;
  if( remarks && number_remarks )
    remarksvec.insert( end(remarksvec), remarks, remarks + number_remarks );
  
  specfile->set_remarks( remarksvec, m );
  return true;
}
  
  
  
bool SpecUtils_SpecFile_set_measurement_source_type( SpecUtils_SpecFile *instance,
                       const SpecUtils_SourceType type,
                       const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  if( !m )
    return false;
  
  const SpecUtils::SourceType st = SpecUtils::SourceType( static_cast<int>(type) );
  specfile->set_source_type( st, m );

  return true;
}


bool SpecUtils_SpecFile_set_measurement_position( SpecUtils_SpecFile *instance,
                    const double longitude,
                    const double latitude,
                    const int64_t microseconds_since_unix_epoch,
                    const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  if( !m )
    return false;
    
  SpecUtils::time_point_t tp{};
  tp += chrono::microseconds( microseconds_since_unix_epoch );
  
  specfile->set_position( longitude, latitude, tp, m );

  return true;
}
  

bool SpecUtils_SpecFile_set_measurement_title( SpecUtils_SpecFile *instance,
                 const char * const title,
                 const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  const shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  if( m )
    specfile->set_title( title ? title : "", m );
  
  return !!m; 
}

  
bool SpecUtils_SpecFile_set_measurement_contained_neutrons( SpecUtils_SpecFile *instance,
                              const bool contained, const float counts,
                              const float neutron_live_time,
                              const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  const shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  try
  {
    if( m )
      specfile->set_contained_neutrons( contained, counts, m, neutron_live_time );
  }catch( std::exception &e )
  {
    return false;
  }
  
  return !!m;
}


bool SpecUtils_SpecFile_set_measurement_energy_calibration( SpecUtils_SpecFile *instance,
                         SpecUtils_CountedRef_EnergyCal *energy_cal,
                         const SpecUtils_Measurement * const measurement )
{
  SpecUtils::SpecFile *specfile = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  const shared_ptr<const SpecUtils::Measurement> m = get_shared_ptr( specfile, measurement );
  shared_ptr<const SpecUtils::EnergyCalibration> *cal
                = reinterpret_cast<shared_ptr<const SpecUtils::EnergyCalibration> *>( energy_cal );
  
  try
  {
    if( !m )
      throw runtime_error( "Invalid measurement" );
    if( !cal )
      throw runtime_error( "null energy cal" );
    
    specfile->set_energy_calibration( *cal, m );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_SpecFile_set_measurement_energy_calibration: " << e.what() << endl;
    return false;
  }
  
  return true;
}


SpecUtils_Measurement *SpecUtils_Measurement_create()
{
  SpecUtils::Measurement *m = new SpecUtils::Measurement();
  return reinterpret_cast<SpecUtils_Measurement *>( m );
}


SpecUtils_Measurement * SpecUtils_Measurement_clone( const SpecUtils_Measurement * const instance )
{
  try
  {
    auto original = reinterpret_cast<const SpecUtils::Measurement *>( instance );
    if( !original )
      throw runtime_error( "null input." );
    
    SpecUtils::Measurement *our_copy = new SpecUtils::Measurement();
    
    *our_copy = *original;
    
    return reinterpret_cast<SpecUtils_Measurement *>( our_copy );
  }catch( std::exception &e )
  {
    // Really dont expect to get here
    cerr << "SpecUtils_Measurement_clone: " << e.what() << endl;
  }
  
  return nullptr;
}
  

void SpecUtils_Measurement_destroy( SpecUtils_Measurement *instance )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    delete m;
}


uint32_t SpecUtils_Measurement_memmorysize( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? static_cast<uint32_t>(m->memmorysize()) : uint32_t(0);
}


void SpecUtils_Measurement_set_equal( SpecUtils_Measurement *lhs, const SpecUtils_Measurement *rhs )
{
  try
  {
    auto lhs_raw = reinterpret_cast<SpecUtils::Measurement *>( lhs );
    auto rhs_raw = reinterpret_cast<const SpecUtils::Measurement *>( rhs );
    if( !lhs_raw )
      throw runtime_error( "null lhs" );
    
    if( !rhs_raw )
      throw runtime_error( "null lhs" );
    
    *lhs_raw = *rhs_raw;
  }catch( std::exception &e )
  {
    // Really dont expect to get here - so we arent returning an error code or anything
    cerr << "SpecUtils_Measurement_set_equal: " << e.what() << endl;
  }
}


void SpecUtils_Measurement_reset( SpecUtils_Measurement *instance )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    m->reset();
}


const char *SpecUtils_Measurement_description( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return "";
  
  const vector<string> &remarks = m->remarks();
  
  for( size_t i = 0; i < remarks.size(); ++i )
  {
    if( SpecUtils::istarts_with(remarks[i], "Description:") )
    {
      const char *answer = remarks[i].c_str() + 12;
      while( *answer && ((*answer) == ' ') )
        ++answer;
      return answer;
    }
  }//
  
  return "";
}


void SpecUtils_Measurement_set_description( SpecUtils_Measurement *instance,
                                      const char * const description_cstr )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return;
  
  // We will erase existing description if empty string is passed in.
  //  Otherwise we will update description.
  string description( description_cstr ? description_cstr : "" );
  if( !description.empty() )
    description = "Description: " + description;
  
  vector<string> remarks = m->remarks();

  // If there is already a description, over-write it
  for( size_t i = 0; i < remarks.size(); ++i )
  {
    if( SpecUtils::istarts_with(remarks[i], "Description:") )
    {
      if( description.empty() )
        remarks.erase( begin(remarks) + i );
      else
        remarks[i] = description;
      
      m->set_remarks( remarks );
      return;
    }
  }//
  
  remarks.push_back( description );
  m->set_remarks( remarks );
}


const char *SpecUtils_Measurement_source_string( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return "";
  
  const vector<string> &remarks = m->remarks();
  
  for( size_t i = 0; i < remarks.size(); ++i )
  {
    if( SpecUtils::istarts_with(remarks[i], "Source:") )
    {
      const char *answer = remarks[i].c_str() + 7;
      while( *answer && ((*answer) == ' ') )
        ++answer;
      return answer;
    }
  }//
  
  return "";
}


void SpecUtils_Measurement_set_source_string( SpecUtils_Measurement *instance,
                                        const char * const source_string_cstr )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return;
  
  // We will erase existing description if empty string is passed in.
  //  Otherwise we will update description.
  string src_string( source_string_cstr ? source_string_cstr : "" );
  if( !src_string.empty() )
    src_string = "Source: " + src_string;
  
  vector<string> remarks = m->remarks();

  // If there is already a source string, over-write it
  for( size_t i = 0; i < remarks.size(); ++i )
  {
    if( SpecUtils::istarts_with(remarks[i], "Source:") )
    {
      if( src_string.empty() )
        remarks.erase( begin(remarks) + i );
      else
        remarks[i] = src_string;
      
      m->set_remarks( remarks );
      return;
    }
  }//
  
  remarks.push_back( src_string );
  m->set_remarks( remarks );
}
  

void SpecUtils_Measurement_set_gamma_counts( SpecUtils_Measurement *instance,
                                       const float *counts,
                                       const uint32_t nchannels,
                                       const float live_time,
                                       const float real_time )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  assert( counts || (nchannels == 0) ); //If `counts` is null, then `nchannels` better be zero to.
  
  if( !m )
    return;
  
  shared_ptr<vector<float>> cc = counts ? make_shared<vector<float>>(counts, counts + nchannels)
                                        : nullptr;
  m->set_gamma_counts( cc, live_time, real_time );
}


void SpecUtils_Measurement_set_neutron_counts( SpecUtils_Measurement *instance,
                                         const float * const counts,
                                         const uint32_t num_tubes,
                                         const float neutron_live_time )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  assert( counts || (num_tubes == 0) ); //If `counts` is null, then `num_tubes` better be zero to.
  
  if( !m )
    return;
  
  const vector<float> nc = counts ? vector<float>(counts, counts + num_tubes) : vector<float>{};
  m->set_neutron_counts( nc, neutron_live_time );
}//SpecUtils_Measurement_set_neutron_counts(...)


const char *SpecUtils_Measurement_title( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  
  const string &title = m->title();
  return title.c_str();
}


void SpecUtils_Measurement_set_title( SpecUtils_Measurement *instance,
                                const char * const title )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m && title );
  if( m )
    m->set_title( title ? title : "" );
}
  

int64_t SpecUtils_Measurement_start_time_usecs( SpecUtils_Measurement *instance )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return 0;
  
  SpecUtils::time_point_t st = m->start_time();
  if( SpecUtils::is_special(st) )
    return 0;
  
  const SpecUtils::time_point_t epoch{};
  const auto total_time = chrono::duration_cast<chrono::microseconds>(st - epoch);
  
  return total_time.count();
}
  

void SpecUtils_Measurement_set_start_time_usecs( SpecUtils_Measurement *instance,
                                           const int64_t start_time )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  
  SpecUtils::time_point_t tp{};
  tp += std::chrono::microseconds( start_time );
  if( m )
    m->set_start_time( tp );
}


bool SpecUtils_Measurement_set_start_time_str( SpecUtils_Measurement *instance,
                                            const char * const start_time_str )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  
  SpecUtils::time_point_t tp{};
  if( start_time_str )
    tp = SpecUtils::time_from_string( start_time_str );
  
  if( m )
    m->set_start_time( tp );
  
  return m && !SpecUtils::is_special(tp);
}
  
char SpecUtils_Measurement_pcf_tag( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->pcf_tag() : '\0';
}


void SpecUtils_Measurement_set_pcf_tag( SpecUtils_Measurement *instance,
                                  const char tag_char )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    m->set_pcf_tag( tag_char );
}
  
uint32_t SpecUtils_Measurement_number_gamma_channels( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? static_cast<uint32_t>(m->num_gamma_channels()) : uint32_t(0);
}
  

const float *SpecUtils_Measurement_gamma_channel_counts( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  const shared_ptr<const vector<float>> &counts = m->gamma_channel_contents();
  return counts ? counts->data() : nullptr;
}


const float *SpecUtils_Measurement_energy_bounds( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  
  const shared_ptr<const vector<float>> &energies = m->channel_energies();
  return energies ? energies->data() : nullptr;
}

const SpecUtils_EnergyCal *
SpecUtils_Measurement_energy_calibration( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  
  shared_ptr<const SpecUtils::EnergyCalibration> cal = m->energy_calibration();
  return cal ? reinterpret_cast<const SpecUtils_EnergyCal *>( cal.get() ) : nullptr;
}
  

const SpecUtils_CountedRef_EnergyCal *
SpecUtils_Measurement_energy_calibration_ref( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  
  shared_ptr<const SpecUtils::EnergyCalibration> cal = m->energy_calibration();
  if( !cal )
    return nullptr;
  
  auto new_cal_ptr = new shared_ptr<const SpecUtils::EnergyCalibration>( cal );
  return reinterpret_cast<const SpecUtils_CountedRef_EnergyCal *>( new_cal_ptr );
}


float SpecUtils_Measurement_real_time( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->real_time() : 0.0f;
}


float SpecUtils_Measurement_live_time( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->live_time() : 0.0f;
}
  

float SpecUtils_Measurement_neutron_live_time( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->neutron_live_time() : 0.0f;
}
  

double SpecUtils_Measurement_gamma_count_sum( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->gamma_count_sum() : 0.0;
}
  

double SpecUtils_Measurement_neutron_count_sum( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->neutron_counts_sum() : 0.0;
}


bool SpecUtils_Measurement_is_occupied( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? (m->occupied() == SpecUtils::OccupancyStatus::Occupied) : false;
}


bool SpecUtils_Measurement_contained_neutron( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->contained_neutron() : false;
}


int SpecUtils_Measurement_sample_number( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->sample_number() : -999;
}

void SpecUtils_Measurement_set_sample_number( SpecUtils_Measurement *instance,
                                        const int samplenum )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    m->set_sample_number( samplenum );
}


const char *SpecUtils_Measurement_detector_name( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  const string &name = m->detector_name();
  return name.c_str();
}


void SpecUtils_Measurement_set_detector_name( SpecUtils_Measurement *instance,
                                        const char *name )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    m->set_detector_name( name ? name : "" );
}


float SpecUtils_Measurement_speed( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->speed() : 0.0f;
}


enum SpecUtils_OccupancyStatus 
SpecUtils_Measurement_occupancy_status( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  const SpecUtils::OccupancyStatus status = m ? m->occupied() : SpecUtils::OccupancyStatus::Unknown;
  return SpecUtils_OccupancyStatus( static_cast<int>(status) );
}


void SpecUtils_Measurement_set_occupancy_status( SpecUtils_Measurement *instance,
                     const SpecUtils_OccupancyStatus status )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    m->set_occupancy_status( SpecUtils::OccupancyStatus(static_cast<int>(status)) );
}


bool SpecUtils_Measurement_has_gps_info( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->has_gps_info() : false;
}


double SpecUtils_Measurement_latitude( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->latitude() : -999.9;
}

double SpecUtils_Measurement_longitude( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->longitude() : -999.9;
}

int64_t SpecUtils_Measurement_position_time_microsec( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return 0;
  
  const SpecUtils::time_point_t &st = m->start_time();
  const SpecUtils::time_point_t epoch{};
  const auto total_time = chrono::duration_cast<chrono::microseconds>(st - epoch);
  return static_cast<int64_t>( total_time.count() );
}


void SpecUtils_Measurement_set_position( SpecUtils_Measurement *instance,
                                   const double longitude,
                                   const double latitude,
                                   const int64_t position_time_microsec )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return;
  
  SpecUtils::time_point_t dt{};
  if( position_time_microsec )
    dt += std::chrono::microseconds( position_time_microsec );
  
  m->set_position( longitude, latitude, dt );
}


float SpecUtils_Measurement_dose_rate( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->dose_rate() : 0.0f;
}


float SpecUtils_Measurement_exposure_rate( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->exposure_rate() : 0.0f;
}

const char *SpecUtils_Measurement_detector_type( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->detector_type().c_str() : nullptr;
}


enum SpecUtils_QualityStatus
SpecUtils_Measurement_quality_status( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? SpecUtils_QualityStatus(static_cast<int>(m->quality_status()))
          : SpecUtils_QualityStatus::SpecUtils_QualityStatus_Missing;
}


enum SpecUtils_SourceType
SpecUtils_Measurement_source_type( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? SpecUtils_SourceType(static_cast<int>(m->source_type()))
          : SpecUtils_SourceType::SpecUtils_SourceType_Unknown;
}


void SpecUtils_Measurement_set_source_type( SpecUtils_Measurement *instance,
                                      const SpecUtils_SourceType type )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  if( m )
    m->set_source_type( SpecUtils::SourceType(static_cast<int>(type)) );
}
  

uint32_t SpecUtils_Measurement_number_remarks( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? static_cast<uint32_t>(m->remarks().size()) : uint32_t(0);
}


const char *SpecUtils_Measurement_remark( const SpecUtils_Measurement * const instance,
                             const uint32_t remark_index )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  
  const vector<string> &remarks = m->remarks();
  return (remark_index < remarks.size()) ? remarks[remark_index].c_str() : nullptr;
}


void SpecUtils_Measurement_set_remarks( SpecUtils_Measurement *instance,
                                  const char **remarks,
                                  const uint32_t number_remarks )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  
  if( !m )
    return;
  if( !remarks || (number_remarks == 0) )
    m->set_remarks( {} );
  else
    m->set_remarks( vector<string>(remarks, remarks+number_remarks) );
}


uint32_t SpecUtils_Measurement_number_parse_warnings( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? static_cast<uint32_t>(m->parse_warnings().size()) : uint32_t(0);
}


const char *SpecUtils_Measurement_parse_warning( const SpecUtils_Measurement * const instance,
                                    const uint32_t index )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  if( !m )
    return nullptr;
  
  const vector<string> &warnings = m->parse_warnings();
  return (index < warnings.size()) ? warnings[index].c_str() : nullptr;
}
  

double SpecUtils_Measurement_gamma_integral( const SpecUtils_Measurement * const instance,
                                     const float lower_energy, const float upper_energy )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->gamma_integral( lower_energy, upper_energy ) : 0.0;
}
  

double SpecUtils_Measurement_gamma_channels_sum( const SpecUtils_Measurement * const instance,
                                         const uint32_t startbin,
                                         const uint32_t endbin )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->gamma_channels_sum( startbin, endbin ) : 0.0;
}


uint32_t SpecUtils_Measurement_derived_data_properties( const SpecUtils_Measurement * const instance )
{
  auto m = reinterpret_cast<const SpecUtils::Measurement *>( instance );
  assert( m );
  return m ? m->derived_data_properties() : uint32_t(0);
}
  

bool SpecUtils_Measurement_combine_gamma_channels( SpecUtils_Measurement *instance,
                                             const uint32_t nchannel )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  assert( m );
  
  try
  {
    if( !m )
      throw runtime_error( "null input" );
    m->combine_gamma_channels( nchannel );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_Measurement_combine_gamma_channels: " << e.what() << endl;
    return false;
  }
  
  return true;
}
  

bool SpecUtils_Measurement_rebin( SpecUtils_Measurement *instance,
                            const SpecUtils_CountedRef_EnergyCal * const cal_ptr )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  const shared_ptr<const SpecUtils::EnergyCalibration> *cal
                    = reinterpret_cast<const shared_ptr<const SpecUtils::EnergyCalibration> *>( cal_ptr );
  assert( m && cal );
  
  try
  {
    if( !cal )
      throw runtime_error( "nullptr input energy cal" );
    
    m->rebin( *cal );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_Measurement_rebin: " << e.what() << endl;
    return false;
  }
  
  return true;
}
  

bool SpecUtils_Measurement_set_energy_calibration( SpecUtils_Measurement *instance,
                                             const SpecUtils_CountedRef_EnergyCal * const cal_ptr )
{
  auto m = reinterpret_cast<SpecUtils::Measurement *>( instance );
  const shared_ptr<const SpecUtils::EnergyCalibration> *cal
                    = reinterpret_cast<const shared_ptr<const SpecUtils::EnergyCalibration> *>( cal_ptr );
  assert( m && cal );
  try
  {
    if( !cal )
      throw runtime_error( "nullptr input energy cal" );
    
    m->set_energy_calibration( *cal );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_Measurement_set_energy_calibration: " << e.what() << endl;
    return false;
  }
  
  return true;
}


SpecUtils_EnergyCal *SpecUtils_EnergyCal_create()
{
  SpecUtils::EnergyCalibration *cal = new SpecUtils::EnergyCalibration();
  return reinterpret_cast<SpecUtils_EnergyCal *>( cal );
}


void SpecUtils_EnergyCal_destroy( SpecUtils_EnergyCal *instance )
{
  auto cal = reinterpret_cast<SpecUtils::EnergyCalibration *>( instance );
  assert( cal );
  if( cal )
    delete cal;
}


SpecUtils_CountedRef_EnergyCal *SpecUtils_CountedRef_EnergyCal_create()
{
  auto obj_ptr = new shared_ptr<SpecUtils::EnergyCalibration>();
  *obj_ptr = make_shared<SpecUtils::EnergyCalibration>();
  return reinterpret_cast<SpecUtils_CountedRef_EnergyCal *>( obj_ptr );
}

    
void SpecUtils_CountedRef_EnergyCal_destroy( SpecUtils_CountedRef_EnergyCal *instance )
{
  auto ptr = reinterpret_cast<shared_ptr<SpecUtils::EnergyCalibration> *>( instance );
  assert( ptr );
  if( ptr )
    delete ptr;
}


const SpecUtils_EnergyCal *SpecUtils_EnergyCal_ptr_from_ref( SpecUtils_CountedRef_EnergyCal *instance )
{
  shared_ptr<const SpecUtils::EnergyCalibration> *cal
                = reinterpret_cast<shared_ptr<const SpecUtils::EnergyCalibration> *>( instance );
  
  return cal ? reinterpret_cast<const SpecUtils_EnergyCal *>(cal->get()) : nullptr;
}
   

SpecUtils_CountedRef_EnergyCal *SpecUtils_EnergyCal_make_counted_ref( SpecUtils_EnergyCal *instance )
{
  auto ptr = reinterpret_cast<SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return nullptr;
  
  auto obj_ptr = new shared_ptr<SpecUtils::EnergyCalibration>(ptr);
  return reinterpret_cast<SpecUtils_CountedRef_EnergyCal *>( obj_ptr );
}
    


SpecUtils_EnergyCalType SpecUtils_EnergyCal_type( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return SpecUtils_EnergyCalType::SpecUtils_EnergyCal_InvalidEquationType;
  
  return SpecUtils_EnergyCalType( static_cast<int>( ptr->type() ) );
}

bool SpecUtils_EnergyCal_valid( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr && ptr->valid();
}

    
uint32_t SpecUtils_EnergyCal_number_coefficients( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr ? static_cast<uint32_t>(ptr->coefficients().size()) : uint32_t(0);
}
    
const float *SpecUtils_EnergyCal_coefficients( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return nullptr;
  const vector<float> &coefs = ptr->coefficients();
  return coefs.data();
}

uint32_t SpecUtils_EnergyCal_number_deviation_pairs( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return 0;
  
  const vector<pair<float,float>> &devs = ptr->deviation_pairs();
  return static_cast<uint32_t>( devs.size() );
}
    
float SpecUtils_EnergyCal_deviation_energy( const SpecUtils_EnergyCal * const instance,
                                       const uint32_t index )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return 0.0f;
  
  const vector<pair<float,float>> &devs = ptr->deviation_pairs();
  if( static_cast<size_t>(index) >= devs.size() )
    return 0.0f;
  
  return devs[index].first;
}

float SpecUtils_EnergyCal_deviation_offset( const SpecUtils_EnergyCal * const instance,
                                         const uint32_t index )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return 0.0f;
  
  const vector<pair<float,float>> &devs = ptr->deviation_pairs();
  if( static_cast<size_t>(index) >= devs.size() )
    return 0.0f;
  
  return devs[index].second;
}
    
uint32_t SpecUtils_EnergyCal_number_channels( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr ? static_cast<uint32_t>(ptr->num_channels()) : uint32_t(0);
}

const float *SpecUtils_EnergyCal_channel_energies( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  if( !ptr )
    return nullptr;
  
  const shared_ptr<const vector<float>> &channel_energies = ptr->channel_energies();
  return channel_energies ? channel_energies->data() : nullptr;
}
    
  /** Sets the polynomial coefficients, and non-linear deviation pairs for the energy calibration object.
   
   @param instance The `SpecUtils_EnergyCal` to modify.
   @param num_channels The number of channels this energy calibration is for.
   @param coeffs The array of polynomial energy calibration coefficients.
   @param number_coeffs The number of entries in the `coeffs` array.  Must be at least two coefficients.
   @param dev_pairs An array giving deviation pairs where the entries are energy followed by offset, e.g.,
          for 3 deviations pairs, the entries in this array would be: [energy_0, offset_0, energy_1, offset_1, energy_2, offset_2]
          May be `NULL`.
   @param number_dev_pairs The number of deviation pairs in the `dev_pairs` array; that is the
          `dev_pairs` array must have twice this many entries in it.
   @returns If the energy calibration supplied is valid, and hence the `SpecUtils_EnergyCal` instance updated.
            Will return false if coefficients or deviation pairs are invalid (e.g., not enough coefficients, NaN of Inf coefficients,
            results in non monotonically increasing channel energies, or are otherwise unreasonable).
   */
bool SpecUtils_EnergyCal_set_polynomial( SpecUtils_EnergyCal * instance,
                                     const uint32_t num_channels,
                                     const float *coeffs,
                                     const uint32_t number_coeffs,
                                     const float * const dev_pairs,
                                     const uint32_t number_dev_pairs )
{
  
  try
  {
    auto ptr = reinterpret_cast<SpecUtils::EnergyCalibration *>( instance );
    assert( ptr );
    if( !ptr || !coeffs )
      throw runtime_error( "nullptr passed in" );
    
    const vector<float> coeffsvec( coeffs, coeffs + number_coeffs );
    vector<pair<float,float>> dev_pairs_vec( number_dev_pairs );
    for( uint32_t i = 0; i < number_dev_pairs; ++i )
    {
      dev_pairs_vec[i].first = dev_pairs[2*i];
      dev_pairs_vec[i].second = dev_pairs[2*i + 1];
    }
    
    ptr->set_polynomial( num_channels, coeffsvec, dev_pairs_vec );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_EnergyCal_set_polynomial: " << e.what() << endl;
    return false;
  }
  
  return true;
}


bool SpecUtils_EnergyCal_set_full_range_fraction( SpecUtils_EnergyCal * instance,
                                              const uint32_t num_channels,
                                              const float *coeffs,
                                              const uint32_t num_coeffs,
                                              const float * const dev_pairs,
                                              const uint32_t number_dev_pairs )
{
  try
  {
    auto ptr = reinterpret_cast<SpecUtils::EnergyCalibration *>( instance );
    assert( ptr );
    if( !ptr || !coeffs )
      throw runtime_error( "nullptr passed in" );
    
    const vector<float> coeffs_vec( coeffs, coeffs + num_coeffs );
    vector<pair<float,float>> dev_pairs_vec( number_dev_pairs );
    for( uint32_t i = 0; i < number_dev_pairs; ++i )
    {
      dev_pairs_vec[i].first = dev_pairs[2*i];
      dev_pairs_vec[i].second = dev_pairs[2*i + 1];
    }
    
    ptr->set_full_range_fraction( num_channels, coeffs_vec, dev_pairs_vec );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_EnergyCal_set_full_range_fraction: " << e.what() << endl;
    return false;
  }
  
  return true;
}


bool SpecUtils_EnergyCal_set_lower_channel_energy( SpecUtils_EnergyCal * instance,
                                              const uint32_t num_channels,
                                              const uint32_t num_energies,
                                              const float * const channel_energies )
{
  try
  {
    auto ptr = reinterpret_cast<SpecUtils::EnergyCalibration *>( instance );
    assert( ptr );
    if( !ptr || !channel_energies )
      throw runtime_error( "nullptr passed in" );
    
    vector<float> coeffs( channel_energies, channel_energies + num_energies );
    ptr->set_lower_channel_energy( num_channels, std::move(coeffs) );
  }catch( std::exception &e )
  {
    cerr << "SpecUtils_EnergyCal_set_lower_channel_energy: " << e.what() << endl;
    return false;
  }
  
  return true;
}


double SpecUtils_EnergyCal_channel_for_energy( const SpecUtils_EnergyCal * const instance,
                                           const double energy )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr ? ptr->channel_for_energy( energy ) : -999.9;
}


double SpecUtils_EnergyCal_energy_for_channel( const SpecUtils_EnergyCal * const instance,
                                           const double channel )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr ? ptr->energy_for_channel( channel ) : -999.9;
}

float SpecUtils_EnergyCal_lower_energy( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr ? ptr->lower_energy() : -999.9f;
}

  
float SpecUtils_EnergyCal_upper_energy( const SpecUtils_EnergyCal * const instance )
{
  auto ptr = reinterpret_cast<const SpecUtils::EnergyCalibration *>( instance );
  assert( ptr );
  return ptr ? ptr->upper_energy() : -999.9f;
}
    
    
    
    /*
    //Free standing energy calibration functions that could be useful to expose to C
     
    std::shared_ptr<EnergyCalibration> energy_cal_combine_channels( const EnergyCalibration &orig_cal,
                                                                   const size_t num_channel_combine );
    double fullrangefraction_energy( const double channel_number,
                                   const std::vector<float> &coeffs,
                                   const size_t nchannel,
                                   const std::vector<std::pair<float,float>> &deviation_pairs );
    double polynomial_energy( const double channel_number,
                             const std::vector<float> &coeffs,
                             const std::vector<std::pair<float,float>> &deviation_pairs );
    std::vector<float>
    polynomial_coef_to_fullrangefraction( const std::vector<float> &coeffs,
                                          const size_t nchannel );
    std::vector<float>
    fullrangefraction_coef_to_polynomial( const std::vector<float> &coeffs,
                                          const size_t nchannel );
    bool calibration_is_valid( const EnergyCalType type,
                                     const std::vector<float> &eqn,
                                     const std::vector< std::pair<float,float> > &devpairs,
                                     size_t nbin );
  */
