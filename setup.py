from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext
from pathlib import Path
from glob import glob
import os
import sys
import tomli


# SSOT __version__ from pyproject.toml
version = tomli.loads(Path("pyproject.toml").read_text(encoding="utf-8"))["project"][
    "version"
]


SRC_FILES = list(glob(os.path.join("src", "*.cpp")))
INCLUDE_DIRS = ["include"]
CXX_STD = 17  # can also be >=c++11

ext_modules = [
    Pybind11Extension(
        "ppef",
        SRC_FILES,
        include_dirs=INCLUDE_DIRS,
        cxx_std=CXX_STD,
        define_macros=[("VERSION_INFO", f'"{version}"')],
    )
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
