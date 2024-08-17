#ifndef CUFILE_CONTEXT_H_
#define CUFILE_CONTEXT_H_

#include <cstddef>
#include <glog/logging.h>
#include <unistd.h>
#ifdef USE_CUDA

#include "cufile.h"
#include <cstring>
#include <string>
#include <system_error>
#include <fcntl.h>

static inline const char *GetCuErrorString(CUresult curesult)
{
    const char *descp;
    if (cuGetErrorName(curesult, &descp) != CUDA_SUCCESS)
        descp = "unknown cuda error";
    return descp;
}

static std::string cuFileGetErrorString(int status)
{
    status = std::abs(status);
    return IS_CUFILE_ERR(status) ? std::string(CUFILE_ERRSTR(status)) : std::string(std::strerror(status));
}

static std::string cuFileGetErrorString(CUfileError_t status)
{
    std::string errStr = cuFileGetErrorString(static_cast<int>(status.err));
    if (IS_CUDA_ERR(status))
        errStr.append(".").append(GetCuErrorString(status.cu_err));
    return errStr;
}

#define CUFILE_CHECK(e)                                                                                            \
    do                                                                                                             \
    {                                                                                                              \
        if (e.err != CU_FILE_SUCCESS)                                                                              \
        {                                                                                                          \
            throw std::runtime_error(cuFileGetErrorString(e) + " @ " + __FILE__ + ":" + std::to_string(__LINE__)); \
        }                                                                                                          \
    } while (0)

class CuFileContext
{
    CUfileHandle_t handle = NULL;
    CUfileDescr_t desc;

public:
    CUfileHandle_t getHandle() const { return handle; }

    /// Create a GDS segment from file name. Return NULL on error.
    explicit CuFileContext(const char* filename)
    {
        int fd = open(filename, O_RDWR | O_DIRECT, 0664);
        LOG(INFO) << "open " << filename << " get " << fd;
        memset(&desc, 0, sizeof(desc));
        desc.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
        desc.handle.fd = fd;

        CUFILE_CHECK(cuFileHandleRegister(&handle, &desc));
    }

    ~CuFileContext()
    {
        
        if (handle)
        {
            cuFileHandleDeregister(handle);
        }
        if (desc.handle.fd)
        {
            close(desc.handle.fd);
        }
    }
};

#endif

#endif