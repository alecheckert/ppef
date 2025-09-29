#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "ppef.h"

namespace py = pybind11;

PYBIND11_MODULE(_ppef, m) {
    py::class_<ppef::SequenceMetadata>(m, "SequenceMetadata", py::module_local());
    py::class_<ppef::Sequence>(m, "Sequence", py::module_local())
        .def(
            py::init<const std::string&>(),
            py::arg("filepath")
        )
        .def(
            py::init<const std::vector<uint64_t>&, uint32_t>(),
            py::arg("values"),
            py::arg("block_size") = 256
        )
        .def_property_readonly("n_elem", &ppef::Sequence::n_elem)
        .def_property_readonly("block_size", &ppef::Sequence::block_size)
        .def_property_readonly("n_blocks", &ppef::Sequence::n_blocks)
        .def("get_meta", &ppef::Sequence::get_meta)
        .def("show_meta", &ppef::Sequence::show_meta)
        .def("save", &ppef::Sequence::save, py::arg("filepath"))
        .def("decode_block", &ppef::Sequence::decode_block, py::arg("block_idx"))
        .def("decode", &ppef::Sequence::decode);
}
