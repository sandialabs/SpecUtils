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

#include <cmath>
#include <vector>
#include <memory>
#include <string>
#include <cctype>
#include <limits>
#include <numeric>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <ctime>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/EnergyCalibration.h"

#include "inja/inja.hpp"

using namespace std;
using namespace inja;
using json = nlohmann::json;

namespace SpecUtils
{
	void to_json(json& j, shared_ptr<const SpecUtils::Measurement> p)
	{
		j["detector_ecal_coeffs"] = p->calibration_coeffs();

		j["real_time"] = p->real_time();
		j["live_time"] = p->live_time();

		j["start_time_iso"] = to_extended_iso_string(p->start_time());

		if (!p->start_time().is_not_a_date_time()) 
		{
			std::tm time_tm = to_tm(p->start_time());
			j["start_time_raw"] = mktime(&time_tm);
		}

		j["gamma_counts"] = (*(p->gamma_counts()));
		j["neutron_counts"] = p->neutron_counts();

		j["gamma_count_sum"] = p->gamma_count_sum();
		j["neutron_counts_sum"] = p->neutron_counts_sum();

		j["remarks"] = p->remarks();

		j["detector_name"] = p->detector_name();
		j["detector_type"] = p->detector_type();

		switch (p->source_type()) {
			case SourceType::Background:
				j["source_type"] = "Background";
				break;
			case SourceType::Calibration:
				j["source_type"] = "Calibration";
				break;
			case SourceType::Foreground:
				j["source_type"] = "Foreground";
				break;
			case SourceType::IntrinsicActivity:
				j["source_type"] = "IntrinsicActivity";
				break;
			case SourceType::Unknown:
			default:
				j["source_type"] = "Unknown";
				break;
		}

		j["latitude"] = p->latitude();
		j["longitude"] = p->longitude();
		j["speed"] = p->speed();
	}

	void to_json(json& j, SpecUtils::DetectorAnalysisResult p)
	{
		j["remark"] = p.remark_;
		j["dose_rate"] = p.dose_rate_;
		j["dose_rate_units"] = "uSv";
		j["real_time"] = p.real_time_;
		j["distance"] = p.distance_;
		j["nuclide"] = p.nuclide_;
		j["nuclide_type"] = p.nuclide_type_;
		j["id_confidence"] = p.id_confidence_;
	}

	void to_json(json& j, shared_ptr<const SpecUtils::DetectorAnalysis> p)
	{
		if (p != NULL)
		{
			j["results"] = p->results_;
			j["algorithm_creator"] = p->algorithm_creator_;
			j["algorithm_name"] = p->algorithm_name_;
			j["algorithm_description"] = p->algorithm_description_;
			j["algorithm_result_description"] = p->algorithm_result_description_;
			j["algorithm_version_components"] = p->algorithm_component_versions_;
		}
		else
		{
			j["results"] = std::vector<SpecUtils::DetectorAnalysisResult>();
		}
	}

	bool SpecFile::write_template(std::ostream& ostr, const std::string template_file) const
	{
		std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

		Environment env;

		// Apply an arbitrary string formatting
		env.add_callback("format", 2, [](Arguments& args) {
			char buffer[256];
			std::string format = args.at(0)->get<string>();
			float value = args.at(1)->get<float>();
			snprintf(buffer, sizeof(buffer), format.c_str(), value);
			return std::string(buffer);
			});

		env.add_callback("format_time", 2, [](Arguments& args) {
			char buffer[256];
			std::string format = args.at(0)->get<string>();

			// Convert passed in value (reverse of mktime above in template variables, assume local time) to a time_tm for formatting
			time_t value = args.at(1)->get<time_t>();
			std::tm* time_tm = localtime(&value);
			strftime(buffer, sizeof(buffer), format.c_str(), time_tm);

			return std::string(buffer);
			});

		// Convert a value in seconds to the N42 format PT<minutes>M<seconds>S
		// Second argument is for seconds precision
		env.add_callback("pt_min_sec", 2, [](Arguments& args) {
			char buffer[256];
			float valueInSeconds = args.at(0)->get<float>();
			int secondsPrecision = args.at(1)->get<int>();
			int minutes = (int)(valueInSeconds / 60);
			float remainingSeconds = valueInSeconds - (60 * minutes);
			char sformat[256];
			snprintf(sformat, sizeof(sformat), "0.%d", secondsPrecision);
			string totalFormat = "PT%dM%" + string(sformat) + "fS";
			snprintf(buffer, sizeof(buffer), totalFormat.c_str(), minutes, remainingSeconds);
			return std::string(buffer);
			});

		// Run the counted zeros compression on the given vector
		env.add_callback("compress_countedzeros", 1, [](Arguments& args) {
			vector<float> compressed_counts;
			compress_to_counted_zeros(args.at(0)->get<std::vector<float>>(), compressed_counts);
			return compressed_counts;
			});

		// Doesn't seem to be any basic math, may need to expand on this
		env.add_callback("subtract", 2, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			float value2 = args.at(1)->get<float>();
			return value1 - value2;
			});

		env.add_callback("modulus", 2, [](Arguments& args) {
			int value1 = args.at(0)->get<int>();
			int value2 = args.at(1)->get<int>();
			return value1 % value2;
			});

		// STEP 1 - read template file
		Template temp;
		try
		{
			temp = env.parse_template(template_file);
		}
		catch (std::exception& e)
		{
			cout << "Error reading input template" << e.what() << endl;
			return false;
		}

		// STEP 2 - populate JSON with data from input spectrum
		json data;

		try
		{
			data["instrument_type"] = instrument_type_;
			data["manufacturer"] = manufacturer_;
			data["instrument_model"] = instrument_model_;
			data["instrument_id"] = instrument_id_;
			data["version_components"] = component_versions_;

			data["measurements"] = measurements_;

			data["gamma_live_time"] = gamma_live_time_;
			data["gamma_real_time"] = gamma_real_time_;

			data["gamma_count_sum"] = gamma_count_sum_;
			data["neutron_counts_sum"] = neutron_counts_sum_;

			data["detector_analysis"] = detectors_analysis_;

			data["remarks"] = remarks_;
		}
		catch (std::exception& e)
		{
			cout << "Error building data structure" << e.what() << endl;
			return false;
		}

		// STEP 3 - render template using JSON data to the provided stream
		try
		{
			env.render_to(ostr, temp, data);
		}
		catch (std::exception& e)
		{
			cout << "Error rendering output file" << e.what() << endl;
			return false;
		}

		return !ostr.bad();
	}//bool write_template( std::ostream& ostr ) const

}//namespace SpecUtils


