# Gamma Spectrum file utilities

[SpecUtils](https://github.com/sandialabs/SpecUtils) is a library for reading, manipulating, and exporting spectrum files produced by RadioIsotope Identification Devices (RIIDs), Radiation Portal Monitors (RPMs), radiation search systems, Personal Radiation Detectors (PRDs), and many laboratory detection systems.
It opens [N42](https://www.nist.gov/programs-projects/ansiieee-n4242-standard) (2006 and 2012 formats), [SPE](https://inis.iaea.org/collection/NCLCollectionStore/_Public/32/042/32042415.pdf), [SPC](https://web.archive.org/web/20160418031030/www.ortec-online.com/download/ortec-software-file-structure-manual.pdf), CSV, TXT, [PCF](https://www.osti.gov/biblio/1378172-pcf-file-format), [DAT](https://www.aseg.org.au/sites/default/files/gr-135.pdf), and many more files; altogether much more than a 100 file format variants.  This library lets you either programmatically access and/or modify the parsed information, or you can save it to any of 13 different formats, or create spectrum files from scratch.


This package is a Python wrapper around the C++ [SpecUtils](https://github.com/sandialabs/SpecUtils) library, with the bindings created using [nanobind](https://github.com/wjakob/nanobind).  [SpecUtils](https://github.com/sandialabs/SpecUtils) is primarily developed as part of [InterSpec](https://github.com/sandialabs/InterSpec/), but is also used by [Cambio](https://github.com/sandialabs/Cambio/), and a number of other projects.

An example use is:
```python
import SpecUtils

# Create a SpecFile object
spec = SpecUtils.SpecFile()

# Load a spectrum file
spec.loadFile("spectrum_file.n42", SpecUtils.ParserType.Auto)

# Get list of all individual records in the spectrum file
meass = spec.measurements() 
print( "There are", len(meass), "spectrum records." )

# Get first measurement
meas = meass[0]

# Get gamma counts, and the lower energy of each channel
counts = meas.gammaCounts()
energies = meas.channelEnergies()

# Get neutron counts
neutrons = meas.neutronCountsSum()

# Get start time
startime = meas.startTime()

# Print out CSV information of energies and counts
print( "StartTime: ", startime )
print( "Neutron counts: ", neutrons, "\n" )
print( "Energy (keV), Counts" )
for i in range(len(counts)):
    print( f"{energies[i]},{counts[i]}" )
```

For further examples, see the [examples](https://github.com/sandialabs/SpecUtils/tree/master/bindings/python/examples) directory.
To run the examples, run the following commands:
```
python test_python.py
python make_file_example.py
python make_html_plot.py /some/path/to/a/file.n42
```


## Installation
You can install the package using pip like this:
```
mkdir my_venv
python -m venv my_venv

pip install SandiaSpecUtils
source my_venv/bin/activate

python
>>> import SpecUtils
>>> spec = SpecUtils.SpecFile()
>>> ...
```


Or instead, you can compile from source yourself, with something like:
```
git clone https://github.com/sandialabs/SpecUtils.git SpecUtils

# Optionsally make/use a virtual environment
mkdir my_venv
python3 -m venv my_venv
source my_venv/bin/activate  # Windows PowerShell: .\my_venv\Scripts\Activate.ps1

# Compile and install the bindings
pip install SpecUtils/bindings/python

# Use the package
python
>>> import SpecUtils
```


## Support
Please create an issue on the [SpecUtils GitHub repository](https://github.com/sandialabs/SpecUtils/issues), or email InterSpec@sandia.gov if you have any questions or problems.
