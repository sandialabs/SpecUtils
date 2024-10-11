#!/usr/bin/bash
export SWIG_LIB=/usr/local/share/swig/4.2.0/
#export SWIG_LIB=/mnt/c/Tools/swig-fortran/Lib

swig -I../../../ -fortran -c++ -outcurrentdir -debug-classes ./SpecUtilsFortran.i

gad_dev=/mnt/c/Projects/code/gadras.worktrees/feature/406-spectra-file-io-out-of-fortran
cpp_wrap_dest=$gad_dev/CGADFunc/FileIO/src
fortran_wrap_dest=$gad_dev/GADRASw/src/SpectraFile

cp SpecUtilsFortran_wrap.cxx $cpp_wrap_dest
cp SpecUtilsWrap.f90 $fortran_wrap_dest
#swig -fortran -c++ -outcurrentdir -debug-classes ./SpecUtilsFortran.i