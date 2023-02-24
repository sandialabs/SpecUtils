# Based on info/examples from 
# https://thomastrapp.com/posts/building-a-pypi-package-for-a-modern-cpp-project/

from sys import platform
from setuptools import setup, dist
from setuptools.command.install import install
import os

# force setuptools to recognize that this is
# actually a binary distribution
class BinaryDistribution(dist.Distribution):
    def has_ext_modules(foo):
        return True

# optional, use README.md as long_description
this_directory = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
with open(os.path.join(this_directory, 'README.md')) as f:
    long_description = f.read()

package_data = []
if platform == "linux" or platform == "linux2":
    print("Linux hasnt been tested yet, sorry")
    exit(1)
elif platform == "darwin":
    package_data = ['SpecUtils.so']
elif platform == "win32":
    package_data = ['SpecUtils.pyd']


setup(
    name='SpecUtils',

    # this package contains one module,
    # which resides in the subdirectory SpecUtils
    packages=['SpecUtils'],

    # make sure the shared library is included
    package_data={'SpecUtils': package_data},
    include_package_data=True,

    description="Spectrum file reading, manipulation, and saving library",
    # optional, the contents of README.md that were read earlier
    long_description=long_description,
    long_description_content_type="text/markdown",

    # See class BinaryDistribution that was defined earlier
    distclass=BinaryDistribution,

    version='0.0.1',
    url='https://github.com/sandialabs/SpecUtils',
    author='William Johnson',
    author_email='wcjohns@sandia.gov',
)