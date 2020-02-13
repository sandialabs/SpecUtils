/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov.
 
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

#include <string>
#include <vector>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/date.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

#include <boost/algorithm/string.hpp>

//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE testIsoTimeString
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>
#include "SpecUtils/UtilityFunctions.h"


using namespace std;
using namespace boost::unit_test;
using namespace boost::posix_time;
using namespace boost::gregorian;


//see http://www.alittlemadness.com/2009/03/31/c-unit-testing-with-boosttest/

//to_extended_iso_string
//to_iso_string
BOOST_AUTO_TEST_CASE( isoString )
{	
    vector<ptime> times;
    vector<greg_month> months;
    months.push_back(Jan);
    months.push_back(Feb);
    months.push_back(Mar);
    months.push_back(Apr);
    months.push_back(May);
    months.push_back(Jun);
    months.push_back(Jul);
    months.push_back(Aug);
    months.push_back(Sep);
    months.push_back(Oct);
    months.push_back(Nov);
    months.push_back(Dec);

    srand(time(NULL));

    //generate random ptime objects from year 1400-2100
    for (int i = 0; i < 100; i++) {
        greg_month mon = months[rand()%12+1];
        int yr = rand()%700+1401;
        int day;
        if (mon == Feb) {
            day = rand()%28+1;
        } else if (mon == Sep || mon == Apr || mon == Jun || mon == Nov) {
            day = rand()%30+1;
        } else {
            day = rand()%31+1;
        }
        date d(yr, mon, day);

        int hr = rand()%24;
        int min = rand()%60;
        int sec = rand()%60;
        int frac = rand()%1000000;
        time_duration td(hr, min, sec, frac);

        ptime t(d, td);
        times.push_back(t);
    }

    //compare to original to_iso_extended_string and to_iso_string function
    //which gives the same thing except with trailing 0's
    for (int i = 0; i < times.size(); i++) {
        ptime pt = times[i];
        string str1 = to_iso_extended_string(pt);
        string str2 = to_iso_string(pt);
        string s1 = UtilityFunctions::to_extended_iso_string(pt);
        string s2 = UtilityFunctions::to_iso_string(pt);
        for (int i = s1.length(); i < 26; i++) {
            s1 += "0";
        }
        for (int i = s2.length(); i < 22; i++) {
            s2 += "0";
        }
        ptime check = from_iso_string(s2);
        bool pass1 = pt==check;
        bool pass2 = s1==str1;
        bool pass3 = s2==str2;
        BOOST_CHECK_MESSAGE(pass1, "failed: "+s2);
        BOOST_CHECK_MESSAGE(pass2, "failed: "+s1);
        BOOST_CHECK_MESSAGE(pass3, "failed: "+s2);
    }
    
    //check special time values
    ptime d1(neg_infin);
    ptime d2(pos_infin);
    ptime d3(not_a_date_time);
    ptime d4(max_date_time);
    ptime d5(min_date_time);
    ptime d6(date(2015,Jul,1));

    bool passneg1 = to_iso_extended_string(d1)==UtilityFunctions::to_extended_iso_string(d1);
    bool passneg2 = to_iso_string(d1)==UtilityFunctions::to_iso_string(d1);
    BOOST_CHECK(!passneg1);
    BOOST_CHECK(!passneg2);

    bool passpos1 = to_iso_extended_string(d2)==UtilityFunctions::to_extended_iso_string(d2);
    bool passpos2 = to_iso_string(d2)==UtilityFunctions::to_iso_string(d2);
    BOOST_CHECK(!passpos1);
    BOOST_CHECK(!passpos2);

    bool not1 = to_iso_extended_string(d3)==UtilityFunctions::to_extended_iso_string(d3);
    bool not2 = to_iso_string(d3)==UtilityFunctions::to_iso_string(d3);
    BOOST_CHECK(not1);
    BOOST_CHECK(not2);

    bool max1 = to_iso_extended_string(d4)==UtilityFunctions::to_extended_iso_string(d4);
    bool max2 = to_iso_string(d4)==UtilityFunctions::to_iso_string(d4);
    BOOST_CHECK(max1);
    BOOST_CHECK(max2);

    bool min1 = to_iso_extended_string(d5)==UtilityFunctions::to_extended_iso_string(d5);
    bool min2 = to_iso_string(d5)==UtilityFunctions::to_iso_string(d5);
    BOOST_CHECK(min1);
    BOOST_CHECK(min2);

    bool mid1 = to_iso_extended_string(d6)==UtilityFunctions::to_extended_iso_string(d6);
    bool mid2 = to_iso_string(d6)==UtilityFunctions::to_iso_string(d6);
    BOOST_CHECK(mid1);
    BOOST_CHECK(mid2);
}
