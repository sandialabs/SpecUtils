$build_dir="c:\temp\build-with-python-bindings\"
$project_dir = Resolve-Path ".\..\..\"

$boost_root="C:\Tools\boost_1_84_install"

mkdir -f $build_dir

pushd $build_dir

if (test-path CMakeCache.txt )
{
    rm -force CMakeCache.txt
}

cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBoost_DEBUG=1 -DSpecUtils_PYTHON_BINDINGS=ON -DBOOST_ROOT="$boost_root" $project_dir 

ninja

popd