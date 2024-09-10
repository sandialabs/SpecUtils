$build_dir="c:\temp\build-with-python-bindings\"
$boost_include_dir="C:\Tools\boost_1_84_install\include\boost-1_84"   
$boost_lib_dir="C:\Tools\boost_1_84_install\lib"
$project_dir = Resolve-Path ".\..\..\"

$boost_root="C:\Tools\boost_1_84_install"

$cmake = "C:\Program Files\CMake\bin\cmake.exe"

mkdir -f $build_dir

pushd $build_dir

if (test-path CMakeCache.txt )
{
    rm -force CMakeCache.txt
}

& $cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBoost_DEBUG=1 -DSpecUtils_PYTHON_BINDINGS=ON -DBOOST_ROOT="$boost_root" $project_dir 

ninja

popd