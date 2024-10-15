#ifdef USE_BOOST_PYTHON

#include <boost/python.hpp>

#include "distributed_object_store.h"

using namespace boost::python;
using namespace mooncake;

BOOST_PYTHON_MODULE(distributed_object_store)
{
    class_<DistributedObjectStore>("DistributedObjectStore", init())
        .def("registerBuffer", &DistributedObjectStore::registerBuffer)
        .def("unregisterBuffer", &DistributedObjectStore::unregisterBuffer)
        .def("get", &DistributedObjectStore::get);
}

#else

#include <pybind11/pybind11.h>

#include "distributed_object_store.h"

using namespace mooncake;
namespace py = pybind11;

PYBIND11_MODULE(distributed_object_store, m) {
    py::class_<DistributedObjectStore>(m, "DistributedObjectStore")
        .def(py::init<>())
        .def("registerBuffer", &DistributedObjectStore::registerBuffer)
        .def("unregisterBuffer", &DistributedObjectStore::unregisterBuffer)
        .def("put", &DistributedObjectStore::put)
        .def("get", &DistributedObjectStore::get);
}

#endif