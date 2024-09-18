#pragma once
#include <vector>
#include <string>

#include <SpecUtils/SpecFile.h>

namespace SpecUtils
{
    namespace PCF
    {
        template <class StringVec>
        inline std::string get_source(const StringVec &vec)
        {
            auto retVal = std::string();
            auto sourceStringOffset = 8U;
            for (auto str : vec)
            {
                if (SpecUtils::istarts_with(str, "Source:"))
                {
                    retVal = str.substr(sourceStringOffset);
                    trim(retVal);
                    break;
                }
            }
            return retVal;
        }

        template <class StringVec>
        inline std::string get_description(const StringVec &vec)
        {
            auto retVal = std::string();
            auto descrOffset = 12U;
            for (auto str : vec)
            {
                if (SpecUtils::istarts_with(str, "Description:"))
                {
                    retVal = str.substr(descrOffset);
                    trim(retVal);
                    break;
                }
            }
            return retVal;
        }

        template <class StringVec>
        inline int get_column(const StringVec &vec)
        {
            auto retVal = std::string();
            auto offset = 7U;
            for (auto str : vec)
            {
                if (SpecUtils::istarts_with(str, "column:"))
                {
                    retVal = str.substr(offset);
                    trim(retVal);
                    break;
                }
            }
            return std::stoi(retVal);
        }

        template <class StringVec>
        inline int get_panel(const StringVec &vec)
        {
            auto retVal = std::string();
            auto offset = 6U;
            for (auto str : vec)
            {
                if (SpecUtils::istarts_with(str, "panel:"))
                {
                    retVal = str.substr(offset);
                    trim(retVal);
                    break;
                }
            }
            return std::stoi(retVal);
        }

        inline void mapDevPairsToArray(const SpecUtils::SpecFile &specFile, float fortranArray[2][20][8][8][4])
        {
            const auto &measurements = specFile.measurements();
            for (auto &m : measurements)
            {
                auto &remarks = m->remarks();
                auto column = get_column(remarks);
                auto panel = get_panel(remarks);
                auto mca = m->detector_number();
                auto &devPairs = m->deviation_pairs();
                auto devPairIdx = 0;
                for (auto &devPair : devPairs)
                {
                    fortranArray[0][devPairIdx][mca][panel][column] = devPair.first;
                    fortranArray[1][devPairIdx][mca][panel][column] = devPair.second;
                    devPairIdx++;
                }
            }
        }

        // Function to map C array to Fortran array
        inline void mapCArrayToFortranArray(const float cArray[4][8][8][20][2], float fortranArray[2][20][8][8][4])
        {
            for (size_t i = 0; i < 4; ++i)
            {
                for (size_t j = 0; j < 8; ++j)
                {
                    for (size_t k = 0; k < 8; ++k)
                    {
                        for (size_t l = 0; l < 20; ++l)
                        {
                            for (size_t m = 0; m < 2; ++m)
                            {
                                fortranArray[m][l][k][j][i] = cArray[i][j][k][l][m];
                            }
                        }
                    }
                }
            }
        }

    } // namespace PCF
} // namespace SpecUtils