#!/usr/bin/env bash

############################################################
# Help                                                     #
############################################################
Help()
{
   # Display Help
   echo "Syntax: ./build_linux.sh [-p|h|c]"
   echo "options:"
   echo "p     Boost library path."
   echo "c     Clean build."
   echo "h     Print this Help."
   echo
}

############################################################
############################################################
# Main program                                             #
############################################################
############################################################

# Set variables
Boost_Path="/usr/include/boost"

############################################################
# Process the input options. Add options as needed.        #
############################################################
# Get the options
while getopts ":hpc:" option; do
   case $option in
      h) # display Help
         Help
         exit;;
      p) # Enter a name
         Boost_Path=$OPTARG;;
      c) # Clean the build
	 rm -rf linux_build && rm -rf node_modules && rm -rf mode_release;;
     \?) # Invalid option
         echo "Error: Invalid option"
         exit;;
   esac
done

npm install cmake-js
cmake-js clean
cmake-js --CDCMAKE_BUILD_TYPE="Release" --CDBOOST_ROOT="$Boost_Path" --out="linux_build"
cmake --build linux_build --target install --config Release

