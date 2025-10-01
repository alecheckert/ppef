#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "ppef.h"

namespace py = pybind11;

ppef::Sequence deserialize(const std::string& s) {
    std::istringstream in(s);
    return ppef::Sequence(in);
}

PYBIND11_MODULE(ppef, m) {
    m.attr("__version__") = VERSION_INFO; // see setup.py
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
        .def(
            py::pickle(
                // __getstate__
                [](const ppef::Sequence& s) {
                    std::string o = s.serialize();
                    return py::bytes(o.data(), o.size());
                },
                // __setstate__
                [](const py::bytes& b) {
                    std::istringstream in(b);
                    return ppef::Sequence(in);
                }
            )
        )
        .def_property_readonly("n_elem", &ppef::Sequence::n_elem)
        .def_property_readonly("block_size", &ppef::Sequence::block_size)
        .def_property_readonly("n_blocks", &ppef::Sequence::n_blocks)
        .def("get_meta", &ppef::Sequence::get_meta)
        .def("info", &ppef::Sequence::info)
        .def("save", &ppef::Sequence::save, py::arg("filepath"))
        .def("decode_block", &ppef::Sequence::decode_block, py::arg("block_idx"))
        .def("decode", &ppef::Sequence::decode)
        .def("__getitem__", &ppef::Sequence::get, py::arg("i"))
        .def("__contains__", &ppef::Sequence::contains, py::arg("q"))
        .def("__len__", &ppef::Sequence::n_elem)
        .def("__and__", &ppef::Sequence::intersect, py::arg("other"))
        .def("__or__", &ppef::Sequence::operator|, py::arg("other"))
        .def(
            "serialize",
            [](const ppef::Sequence& s) {
                std::string o = s.serialize();
                return py::bytes(o.data(), o.size());
            }
        );
    m.def("deserialize", &deserialize, py::arg("serialized"));
}
