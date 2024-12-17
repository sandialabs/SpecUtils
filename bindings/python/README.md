# Python bindings to SpecUtils

[nanobind](https://github.com/wjakob/nanobind) is used to create the Python bindings to SpecUtils.


To compile the bindings, run the following commands:
```
mkdir my_venv
python3 -m venv my_venv

# Activate the virtual environment
# Unix: 
source my_venv/bin/activate
# Windows PowerShell
#.\my_venv\Scripts\Activate.ps1

cd my_venv
pip install /path/to/SpecUtils/bindings/python
```

To run the examples, run the following commands:
```
cd examples
python test_python.py
python make_file_example.py
python make_html_plot.py /some/path/to/a/file.n42
```

To run the tests, run the following commands:
```
python -m unittest tests/test_specutils.py
```

