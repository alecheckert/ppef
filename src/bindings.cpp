#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "ppef.h"

namespace py = pybind11;

PYBIND11_MODULE(_ppef, m) {
    m.def("floor_log2_u64", ppef::floor_log2_u64);
}
