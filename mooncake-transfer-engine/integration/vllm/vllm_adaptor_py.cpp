#include "vllm_adaptor.h"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(distributed_object_store, m)
{
    py::class_<VLLMAdaptor>(m, "VLLMAdaptor")
        .def(py::init<>())
        .def("initialize", &VLLMAdaptor::initialize)
        .def("allocateManagedBuffer", &VLLMAdaptor::allocateManagedBuffer)
        .def("freeManagedBuffer", &VLLMAdaptor::freeManagedBuffer)
        .def("transferSync", &VLLMAdaptor::transferSync);
}