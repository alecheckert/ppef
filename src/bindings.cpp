#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "ppef.h"

namespace py = pybind11;

PYBIND11_MODULE(_ppef, m) {
    py::class_<ppef::PEFMetadata>(m, "PEFMetadata", py::module_local());
    py::class_<ppef::PEF>(m, "PEF", py::module_local())
        .def(
            py::init<const std::string&>(),
            py::arg("filepath")
        )
        .def(
            py::init<const std::vector<uint64_t>&, uint32_t>(),
            py::arg("values"),
            py::arg("block_size") = 256
        )
        .def_property_readonly("n_elem", &ppef::PEF::n_elem)
        .def_property_readonly("block_size", &ppef::PEF::block_size)
        .def_property_readonly("n_blocks", &ppef::PEF::n_blocks)
        .def("get_meta", &ppef::PEF::get_meta)
        .def("show_meta", &ppef::PEF::show_meta)
        .def("save", &ppef::PEF::save, py::arg("filepath"))
        .def("decode_block", &ppef::PEF::decode_block, py::arg("block_idx"))
        .def("decode", &ppef::PEF::decode);
}
