from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext
from glob import glob
import os
import sys


SRC_FILES = list(glob(os.path.join("src", "*.cpp")))
INCLUDE_DIRS = ["include"]
CXX_STD = 17

sys.stderr.write(
    "\nPPEF BUILD CONFIGURATION:\n"
    f"{SRC_FILES=}\n"
    f"{INCLUDE_DIRS=}\n"
    f"{CXX_STD=}\n"
)

ext_modules = [
    Pybind11Extension(
        "_ppef",
        SRC_FILES,
        include_dirs=INCLUDE_DIRS,
        cxx_std=17,
    )
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
