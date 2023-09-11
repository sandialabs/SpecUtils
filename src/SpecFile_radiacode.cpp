/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 Copyright (C) 2023 Chris Kuethe

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

#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"

using namespace std;

namespace SpecUtils {

bool SpecFile::load_radiacode_file(const std::string& filename) {
#ifdef _WIN32
  ifstream input(convert_from_utf8_to_utf16(filename).c_str(),
                 ios_base::binary | ios_base::in);
#else
  ifstream input(filename.c_str(), ios_base::binary | ios_base::in);
#endif

  if (!input.is_open())
    return false;

  const bool success = load_from_radiacode(input);

  if (success)
    filename_ = filename;

  return success;
}  // bool load_radiacode_file( const std::string &filename );

bool SpecFile::load_from_radiacode(std::istream& input) {
  reset();

  if (!input.good())
    return false;

  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);

  // Determine stream size
  input.seekg(0, ios::end);
  const size_t file_size = static_cast<size_t>(input.tellg() - start_pos);
  input.seekg(start_pos);

  // The smallest valid 256 channel RadiaCode file I've been able to construct
  // is about 7KB. Typical 1024-channel foreground RC files are about 27KB going
  // up to 31KB for files with many counts per channel. My largest real file
  // with both foreground and background spectra is 59KB. My largest synthetic
  // dual spectrum file that can load back into the RC app is 68KB.
  //
  // The limits of the numeric formats or the descriptive strings have not been
  // carefully analyzed.
  //
  // Finally, there appears to be space for including a thumbnail of the
  // spectrum plot which might be another 6-7KB, but I have not seen any real
  // world files including a thumbnail.
  //
  // Taken together, 80KB should be sufficient to load a RadiaCode file.

  if (file_size < 7 * 1024 || file_size > 80 * 1024)
    return false;

  string filedata;
  filedata.resize(file_size + 1);

  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0;  // jic.

  // Look for some distinctive strings early in the file
  // If they exist, this is probably a RadiaCode file.
  int signature_max_offset = 512;
  const auto fmtver_pos = filedata.find("<FormatVersion>");
  if (fmtver_pos == string::npos || fmtver_pos > signature_max_offset)
    return false;

  const auto dcr_pos = filedata.find("<DeviceConfigReference>");
  if (dcr_pos == string::npos || dcr_pos > signature_max_offset)
    return false;

  const auto device_model_pos = filedata.find("RadiaCode-");
  if (device_model_pos == string::npos ||
      device_model_pos > signature_max_offset)
    return false;

  if (device_model_pos < dcr_pos)
    return false;

#if PERFORM_DEVELOPER_CHECKS
  cout << "RadiaCode format detected" << endl;
#endif  // PERFORM_DEVELOPER_CHECKS
  try {
    std::shared_ptr<Measurement> fg_meas, bg_meas;

    rapidxml::xml_document<char> doc;
    doc.parse<0>(&(filedata[0]));

#if PERFORM_DEVELOPER_CHECKS
    cout << "RadiaCode XML parsed" << endl;
#endif  // PERFORM_DEVELOPER_CHECKS

    /*
     The RadiaCode XML format has no published specification. In the example
     below, fixed values such as "120920" or "2" which do not appear to change
     between data files are included verbatim; actual varying quantities are
     indicated by their type, such as (float), (integer), or (string).

    ------------------------------------------------------------------------

     <?xml version="1.0"?>
     <ResultDataFile xmlns:xsd="http://www.w3.org/2001/XMLSchema"
     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
       <FormatVersion>(int)</FormatVersion>
       <ResultDataList>
         <ResultData>
           <DeviceConfigReference>
             <Name>RadiaCode-(int)</Name>
           </DeviceConfigReference>

         <BackgroundSpectrumFile>(string)</BackgroundSpectrumFile>

         <StartTime>(isotime)</StartTime>
         <EndTime>(isotime)</EndTime>

         <EnergySpectrum>
           <NumberOfChannels>(integer)</NumberOfChannels>
           <ChannelPitch>1</ChannelPitch>
           <SpectrumName>(string)</SpectrumName>
           <SerialNumber>(string)</SerialNumber>
           <EnergyCalibration>
             <PolynomialOrder>2</PolynomialOrder>
             <Coefficients>
               <Coefficient>(float)</Coefficient>
               <Coefficient>(float)</Coefficient>
               <Coefficient>(float)</Coefficient>
             </Coefficients>
           </EnergyCalibration>
           <MeasurementTime>(integer)</MeasurementTime>
           <Spectrum>
             <DataPoint>(integer)</DataPoint>
             ...
             <DataPoint>(integer)</DataPoint>
           </Spectrum>
         </EnergySpectrum>

         <StartTime>(isotime)</StartTime>
         <EndTime>(isotime)</EndTime>

         <BackgroundEnergySpectrum>
         ...
         </BackgroundEnergySpectrum>

         <Visible>true</Visible>
         <PulseCollection>
           <Format>Base64 encoded binary</Format>
           <Pulses />
         </PulseCollection>

         </ResultData>
       </ResultDataList>
     </ResultDataFile>

     */

    string bg_spec_file, spec_name;
    string start_time;
    vector<float> cal_coefs;

    // Drill down to the <ResultData> node
    const rapidxml::xml_node<char>* n_root = doc.first_node("ResultDataFile")
                                                 ->first_node("ResultDataList")
                                                 ->first_node("ResultData");
    if (!n_root)
      throw runtime_error("unable to find ResultData");

#if PERFORM_DEVELOPER_CHECKS
    cout << "RadiaCode ResultData exists" << endl;
#endif  // PERFORM_DEVELOPER_CHECKS
    for (rapidxml::xml_node<char>* n_cur = n_root->first_node(); n_cur;
         n_cur = n_cur->next_sibling()) {
      string cur_node_name = n_cur->name();

      if (cur_node_name == "DeviceConfigReference") {
        instrument_model_ = n_cur->first_node("Name")->value();
        instrument_type_ = "Spectroscopic Personal Radiation Detector";
        manufacturer_ = "Scan-Electronics";
        detector_type_ = SpecUtils::DetectorType::RadiaCode;

#if PERFORM_DEVELOPER_CHECKS
        cout << "got device type " << instrument_model_ << endl;
#endif  // PERFORM_DEVELOPER_CHECKS
      }

      else if (cur_node_name == "BackgroundSpectrumFile") {
        bg_spec_file = n_cur->value();
      }

      // Start/End time are sibling nodes of EnergySpectrum when they should
      // be children of that node... the same as MeasurementTime. At least
      // they come before their associated EnergySpectrum.
      else if (cur_node_name == "StartTime") {
        start_time = n_cur->value();
      }

      else if (cur_node_name == "EnergySpectrum") {
        if (fg_meas)
          throw runtime_error("File contains more than one EnergySpectrum");
        fg_meas = make_shared<Measurement>();
        fg_meas->source_type_ = SourceType::Foreground;
        fg_meas->real_time_ =
            atof(n_cur->first_node("MeasurementTime")->value());
        fg_meas->start_time_ = SpecUtils::time_from_string(
            start_time, SpecUtils::DateParseEndianType::LittleEndianFirst);
        fg_meas->detector_name_ = n_cur->first_node("SerialNumber")->value();
        instrument_id_ = fg_meas->detector_name_;
        fg_meas->title_ = n_cur->first_node("SpectrumName")->value();

        auto _ec = n_cur->first_node("EnergyCalibration");
        int _po = atoi(_ec->first_node("PolynomialOrder")->value());
        int _nc = atoi(n_cur->first_node("NumberOfChannels")->value());
        auto _sp = n_cur->first_node("Spectrum");

        cal_coefs.clear();
        for (rapidxml::xml_node<char>* x =
                 _ec->first_node("Coefficients")->first_node();
             x; x = x->next_sibling())
          cal_coefs.push_back(atof(x->value()));

        if (2 != _po || 3 != cal_coefs.size())
          throw runtime_error("Invalid FG calibration coefficients");

        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial(_nc, cal_coefs, {});
        fg_meas->energy_calibration_ = newcal;

        auto fg_counts = std::make_shared<vector<float>>();
        for (rapidxml::xml_node<char>* x = _sp->first_node(); x;
             x = x->next_sibling())
          fg_counts->push_back(atoi(x->value()));

        if (fg_counts->size() != _nc)
          throw runtime_error("FG Spectrum length != channel count");

        fg_meas->gamma_counts_ = fg_counts;
        fg_meas->gamma_count_sum_ =
            std::accumulate(begin(*fg_counts), end(*fg_counts), 0.0);
        measurements_.push_back(fg_meas);
#if PERFORM_DEVELOPER_CHECKS
        cout << "RadiaCode foreground spectrum OK" << endl;
#endif  // PERFORM_DEVELOPER_CHECKS
      }

      else if (cur_node_name == "BackgroundEnergySpectrum") {
        if (bg_meas)
          throw runtime_error(
              "File contains more than one BackgroundEnergySpectrum");
        bg_meas = make_shared<Measurement>();

        bg_meas->source_type_ = SourceType::Background;
        bg_meas->real_time_ =
            atof(n_cur->first_node("MeasurementTime")->value());
        bg_meas->start_time_ = SpecUtils::time_from_string(
            start_time, SpecUtils::DateParseEndianType::LittleEndianFirst);
        spec_name = n_cur->first_node("SpectrumName")->value();
        bg_meas->detector_name_ = n_cur->first_node("SerialNumber")->value();

        if (spec_name != bg_spec_file)
          throw runtime_error("Inconsistent background spectrum description");
        bg_meas->title_ = spec_name;

        int _nc = atoi(n_cur->first_node("NumberOfChannels")->value());
        auto _ec = n_cur->first_node("EnergyCalibration");
        int _po = atoi(_ec->first_node("PolynomialOrder")->value());
        auto _sp = n_cur->first_node("Spectrum");

        cal_coefs.clear();
        for (rapidxml::xml_node<char>* x =
                 _ec->first_node("Coefficients")->first_node();
             x; x = x->next_sibling())
          cal_coefs.push_back(atof(x->value()));

        if (2 != _po || 3 != cal_coefs.size())
          throw runtime_error("Invalid BG calibration coefficients");

        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial(_nc, cal_coefs, {});
        bg_meas->energy_calibration_ = newcal;

        auto bg_counts = std::make_shared<vector<float>>();
        for (rapidxml::xml_node<char>* x = _sp->first_node(); x;
             x = x->next_sibling())
          bg_counts->push_back(atoi(x->value()));

        if (bg_counts->size() != _nc)
          throw runtime_error("BG Spectrum length != channel count");
        bg_meas->gamma_counts_ = bg_counts;
        bg_meas->gamma_count_sum_ =
            std::accumulate(begin(*bg_counts), end(*bg_counts), 0.0);
        measurements_.push_back(bg_meas);
#if PERFORM_DEVELOPER_CHECKS
        cout << "RadiaCode background spectrum OK" << endl;
#endif  // PERFORM_DEVELOPER_CHECKS
      }
    }

    cleanup_after_load();
  } catch (std::exception&) {
    reset();
    input.clear();
    input.seekg(start_pos, ios::beg);
    return false;
  }  // try / catch

  return true;
}  // bool load_from_radiacode( std::istream &input )

}  // namespace SpecUtils
