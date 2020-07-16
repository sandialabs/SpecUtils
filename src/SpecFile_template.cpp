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

		j["start_time"] = to_extended_iso_string(p->start_time());

		j["gamma_counts"] = (*(p->gamma_counts()));
		j["neutron_counts"] = p->neutron_counts();

		j["gamma_count_sum"] = p->gamma_count_sum();
		j["neutron_counts_sum"] = p->neutron_counts_sum();
	}

	void to_json(json& j, SpecUtils::DetectorAnalysisResult p)
	{
		j["remark"] = p.remark_;
		j["dose_rate"] = p.dose_rate_;
		j["dose_rate_units"] = "uSv";
		j["real_time"] = p.real_time_;
		j["distance"] = p.distance_;
	}

	void to_json(json& j, shared_ptr<const SpecUtils::DetectorAnalysis> p)
	{
		if (p != NULL)
		{
			j["results"] = p->results_;
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

		// Convert a value in seconds to the N42 format PT<minutes>M<seconds>S
		env.add_callback("pt_min_sec", 1, [](Arguments& args) {
			char buffer[256];
			float valueInSeconds = args.at(0)->get<float>();
			int minutes = (int)(valueInSeconds / 60);
			float remainingSeconds = valueInSeconds - (60 * minutes);
			//TODO: is three digits right here for seconds precision??
			snprintf(buffer, sizeof(buffer), "PT%dM%0.3fS", minutes, remainingSeconds);
			return std::string(buffer);
		});

		// Run the counted zeros compression on the given vector
		env.add_callback("compress_countedzeros", 1, [](Arguments& args) {
			vector<float> compressed_counts;
			compress_to_counted_zeros(args.at(0)->get<std::vector<float>>(), compressed_counts);
			return compressed_counts;
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

			data["measurements"] = measurements_;

			data["gamma_live_time"] = gamma_live_time_;
			data["gamma_real_time"] = gamma_real_time_;
			
			data["gamma_count_sum"] = gamma_count_sum_;
			data["neutron_counts_sum"] = neutron_counts_sum_;

			data["detector_analysis"] = detectors_analysis_;
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


