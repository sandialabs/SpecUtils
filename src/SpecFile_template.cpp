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
#include <ctime>
#include <cctype>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>

#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
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

		if (!SpecUtils::is_special(p->start_time()))
		{
      j["start_time_raw"] = std::chrono::system_clock::to_time_t( p->start_time() );
		}

		j["gamma_counts"] = (*(p->gamma_counts()));
		j["neutron_counts"] = p->neutron_counts();

		j["gamma_count_sum"] = p->gamma_count_sum();
		j["neutron_counts_sum"] = p->neutron_counts_sum();

		j["remarks"] = p->remarks();

		j["detector_name"] = p->detector_name();
		j["detector_type"] = p->detector_type();

		j["sample_number"] = p->sample_number();

		switch (p->occupied()) {
			case OccupancyStatus::Occupied:
				j["occupied"] = "true";
				break;
			case OccupancyStatus::NotOccupied:
				j["occupied"] = "false";
				break;
			case OccupancyStatus::Unknown:
				j["occupied"] = "unknown";
				break;
		}

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
		j["dx"] = 0.1*p->dx();
		j["dy"] = 0.1*p->dy();
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

	bool SpecFile::write_template(std::ostream& ostr, const std::string template_file, bool strip_blocks) const
	{
		std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

		Environment env;

		//You can control the template whitespace processing here, but its tricky and messes with the file line endings
		env.set_trim_blocks(strip_blocks);
		env.set_lstrip_blocks(strip_blocks);

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

		// Convert a value in seconds to the format <hours>:<minutes>:<seconds>
		// Second argument is for seconds precision
		env.add_callback("hr_min_sec", 2, [](Arguments& args) {
			char buffer[256];
			float valueInSeconds = args.at(0)->get<float>();
			int secondsPrecision = args.at(1)->get<int>();
			int hours = (int)(valueInSeconds / 3600);
			float remainingSeconds = valueInSeconds - (60 * 60 * hours);
			int minutes = (int)(remainingSeconds / 60);
			remainingSeconds = remainingSeconds - (60 * minutes);
			char sformat[256];
			snprintf(sformat, sizeof(sformat), "0%d.%d", secondsPrecision+3 /* plus 3 for first two digits plus decimal */, secondsPrecision);
			string totalFormat = "%02d:%02d:%" + string(sformat) + "f";
			snprintf(buffer, sizeof(buffer), totalFormat.c_str(), hours, minutes, remainingSeconds);
			return std::string(buffer);
			});

		// Run the counted zeros compression on the given vector
		env.add_callback("compress_countedzeros", 1, [](Arguments& args) {
			vector<float> compressed_counts;
			compress_to_counted_zeros(args.at(0)->get<std::vector<float>>(), compressed_counts);
			return compressed_counts;
			});

		// Sum two arrays element-wise
		env.add_callback("sum_arrays", 2, [](Arguments& args) {
			vector<float> a = args.at(0)->get<std::vector<float>>();
			vector<float> b = args.at(1)->get<std::vector<float>>();

			assert(a.size() == b.size());

			std::vector<float> result;
			result.reserve(a.size());

			std::transform(a.begin(), a.end(), b.begin(), std::back_inserter(result), std::plus<float>());
			return result;
			});

		// Doesn't seem to be any basic math in inja, so provide some here
		env.add_callback("add", 2, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			float value2 = args.at(1)->get<float>();
			return value1 + value2;
			});

		env.add_callback("subtract", 2, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			float value2 = args.at(1)->get<float>();
			return value1 - value2;
			});

		env.add_callback("multiply", 2, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			float value2 = args.at(1)->get<float>();
			return value1 * value2;
			});

		env.add_callback("divide", 2, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			float value2 = args.at(1)->get<float>();
			return value1 / value2;
			});

		env.add_callback("sqrt", 1, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			return sqrt(value1);
			});

		env.add_callback("pow", 2, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			float value2 = args.at(0)->get<float>();
			return pow(value1, value2);
			});

		env.add_callback("modulus", 2, [](Arguments& args) {
			int value1 = args.at(0)->get<int>();
			int value2 = args.at(1)->get<int>();
			return value1 % value2;
			});

		srand( static_cast<unsigned>(time(NULL)) ); // This is important for the random numbers to work correctly

		env.add_callback("rand", 2, [](Arguments& args) {
			int value1 = args.at(0)->get<int>();
			int value2 = args.at(1)->get<int>();

			// return a random number between [value1,value2] (inclusive)
			// From stdlib notes, this is NOT a true uniform distribution!
			return rand() % (value2 - value1 + 1) + value1;
			});


		env.add_callback("increment", 1, [](Arguments& args) {
			int value1 = args.at(0)->get<int>();
			return ++value1;
			});

		env.add_callback("decrement", 1, [](Arguments& args) {
			int value1 = args.at(0)->get<int>();
			return --value1;
			});

		env.add_callback("truncate", 1, [](Arguments& args) {
			float value1 = args.at(0)->get<float>();
			return (int)value1;
			});

		// This is to filter a JSON array based on a provided property and value
		env.add_callback("filter", 3, [](Arguments& args) {
			json dataToFilter = args.at(0)->get<json>();
			std::string filterProp = args.at(1)->get<string>();
			std::string filterValue = args.at(2)->get<string>();
			json filtered;

			for (auto& element : dataToFilter) {
				if (element[filterProp] == filterValue) {
					filtered.push_back(element);
				}
			}

			return filtered;

			});

		// For time series data, if you have multiple Poisson samples GADRAS just 
		// outputs all the records in a big lump. We want to reorganize them 
		// so we can more easily step through the sequence and deal with statistics.
		env.add_callback("reslice_data", 4, [](Arguments& args) {
			json dataToProcess = args.at(0)->get<json>();
			int nSamples = args.at(1)->get<int>();
			std::string sourceFilter = args.at(2)->get<std::string>();

			// Some N42 files have each sample duplicated, while some of them have the entire occupancy duplicated with the samples already in order
			// We need to be able to handle it both ways.
			bool samplesInOrder = args.at(3)->get<bool>();

			json organized = {};

			for (int i = 0; i < nSamples; i++) {
				organized[i] = {};
			}

			int sampleCounter = 0;
			int rowCounter = 0;

			int numDataPoints = 0;

			if (samplesInOrder) {
				int filteredSampleCount = 0;

				for (auto& element : dataToProcess) {
					std::string sourceType = element["source_type"];

					if (sourceType != sourceFilter) continue; // Skip this one, not the right source type

					filteredSampleCount++;
				}

				if (nSamples == 0) {
					cerr << "Number of samples is zero!" << endl;
				}
				else {
					numDataPoints = filteredSampleCount / nSamples;
				}
			}

			for (auto& element : dataToProcess) {
				std::string sourceType = element["source_type"];

				if (sourceType != sourceFilter) continue; // Skip this one, not the right source type

				if (samplesInOrder)
				{
					// Samples are in order already so add them sequentially
					organized[rowCounter].push_back(element);

					if (sampleCounter % numDataPoints == (numDataPoints - 1))
					{
						rowCounter++;
					}
				}
				else 
				{
					// Each sample is duplicated before moving on to the next, so reorder things here
					organized[sampleCounter % nSamples].push_back(element);
				}

				sampleCounter++;
			}

			return organized;

			});

		// Sum the counts in the provided channels, using interpolation if needed for fractional bounds on the energy window
		env.add_callback("sum_counts_in_window", 3, [](Arguments& args) {
			vector<float> counts = args.at(0)->get<std::vector<float>>();
			float lld = args.at(1)->get<float>();
			float uld = args.at(2)->get<float>();

			float sum = 0;
			float chn_last = 0;

			for (int i = 0; i < counts.size(); i++) {
				int chnNbr = i + 1;
				if (chnNbr >= lld && chnNbr < uld) {
					float lld_extra = 0;
					float uld_minus = 0;

					float lld_interp = chnNbr - lld;
					float uld_interp = uld - chnNbr;

					float chn = counts.at(i);

					if (lld_interp < 1 && lld_interp > 0) {
						// do the interpolation on the lower end of the energy window
						lld_extra = lld_interp * chn_last;
					}
					if (uld_interp < 1 && uld_interp > 0) {
						//do the interpolation on the Upper end of the energy window
						uld_minus = (1 - uld_interp) * chn;
					}
					
					sum += chn + lld_extra - uld_minus;
					chn_last = chn;
				}
			}

			return sum;
			});

		env.add_callback("init_queue", 1, [](Arguments& args) {
			int sizeParam = args.at(0)->get<int>();

			json arr;
			for (int i = 0; i < sizeParam; i++) {
				arr.push_back(0);
			}
			return arr;
			});

		env.add_callback("push_queue", 2, [](Arguments& args) {
			json arr = args.at(0)->get<json>();
			float newValue = args.at(1)->get<float>();

			arr.erase(0);
			arr.push_back(newValue);

			return arr;
			});

		env.add_callback("sum_queue", 1, [](Arguments& args) {
			json arr = args.at(0)->get<json>();

			double sum = 0;
			for (auto& element : arr) {
				sum += element.get<double>();
			}
			return static_cast<float>(sum);
			});

		env.add_callback("template_error", 1, [](Arguments& args) {
			std::string message = args.at(0)->get<std::string>();

			cerr << "Template Error: " << message << endl;

			return message;
			});

		// STEP 1 - read template file
		Template temp;
		try
		{
			cout << "Parsing template '" << template_file << "'" << endl;
			temp = env.parse_template(template_file);
		}
		catch (std::exception& e)
		{
			cerr << "Error reading input template " << e.what() << endl;
			return false;
		}

		// STEP 2 - populate JSON with data from input spectrum
		json data;

		try
		{
			cout << "Populating data..." << endl;

			data["instrument_type"] = instrument_type_;
			data["manufacturer"] = manufacturer_;
			data["instrument_model"] = instrument_model_;
			data["instrument_id"] = instrument_id_;
			data["version_components"] = component_versions_;

			data["measurements"] = measurements_;

			cout << "\tFound " << measurements_.size() << " input measurements" << endl;

			data["gamma_live_time"] = gamma_live_time_;
			data["gamma_real_time"] = gamma_real_time_;

			data["gamma_count_sum"] = gamma_count_sum_;
			data["neutron_counts_sum"] = neutron_counts_sum_;

			data["detector_analysis"] = detectors_analysis_;

			data["remarks"] = remarks_;
		}
		catch (std::exception& e)
		{
			cerr << "Error building data structure " << e.what() << endl;
			return false;
		}

		// STEP 3 - render template using JSON data to the provided stream
		try
		{
			cout << "Rendering output file..." << endl;
			env.render_to(ostr, temp, data);
		}
		catch (std::exception& e)
		{
			cerr << "Error rendering output file " << e.what() << endl;
			return false;
		}

		return !ostr.bad();
	}//bool write_template( std::ostream& ostr ) const

}//namespace SpecUtils


