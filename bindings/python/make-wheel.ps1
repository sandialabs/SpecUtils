$build_dir="c:\temp\build-with-python-bindings\"
$py_bind_dir = pwd  

mkdir -f $build_dir/SpecUtils
pushd $build_dir

cp .\libSpecUtils.dll .\SpecUtils\SpecUtils.pyd
cp $py_bind_dir\__init__.py .\SpecUtils\
cp $py_bind_dir\setup.py .\
cp $py_bind_dir\README.md .\
pip install pip setuptools wheel
python.exe setup.py bdist_wheel --plat-name=win_amd64

# version is 0.0.2 as of this writing
python.exe -m pip install --user --force .\dist\SpecUtils-0.0.2-cp311-cp311-win_amd64.whl

popd