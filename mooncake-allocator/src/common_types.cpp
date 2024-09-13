#include "common_types.h"

namespace mooncake
{
    int64_t getError(ERRNO err)
    {
        return static_cast<int64_t>(err);
    }

    const std::string& errnoToString(const int64_t errnoValue)
    {
    static const std::map<int64_t, std::string> errnoMap = {
        {-1, "BUFFER_OVERFLOW"},
        {-2, "SHARD_INDEX_OUT_OF_RANGE"},
        {-3, "AVAILABLE_SEGMENT_EMPTY"},
        {-4, "NO_AVAILABLE_HANDLE"},
        {-5, "INVALID_VERSION"},
        {-6, "INVALID_KEY"},
        {-7, "WRITE_FAIL"},
        {-8, "INVALID_PARAMS"},
        {-9, "INVALID_WRITE"},
        {-10, "INVALID_READ"},
        {-11, "INVALID_REPLICA"}
    };

    auto it = errnoMap.find(errnoValue);
    if (it != errnoMap.end()) {
        return it->second;
    } else {
        static const std::string unknown("UNKNOWN");
        return unknown;
    }
    }

    const std::string& errEnumToString(const ERRNO errno)
    {
        static const std::map<ERRNO, std::string> errnoMap = {
            {ERRNO::BUFFER_OVERFLOW, "BUFFER_OVERFLOW"},
            {ERRNO::SHARD_INDEX_OUT_OF_RANGE, "SHARD_INDEX_OUT_OF_RANGE"},
            {ERRNO::AVAILABLE_SEGMENT_EMPTY, "AVAILABLE_SEGMENT_EMPTY"},
            {ERRNO::NO_AVAILABLE_HANDLE, "NO_AVAILABLE_HANDLE"},
            {ERRNO::INVALID_VERSION, "INVALID_VERSION"},
            {ERRNO::INVALID_KEY, "INVALID_KEY"},
            {ERRNO::WRITE_FAIL, "WRITE_FAIL"},
            {ERRNO::INVALID_PARAMS, "INVALID_PARAMS"},
            {ERRNO::INVALID_WRITE, "INVALID_WRITE"},
            {ERRNO::INVALID_READ, "INVALID_READ"},
            {ERRNO::INVALID_REPLICA, "INVALID_REPLICA"}
        };

        auto it = errnoMap.find(errno);
        if (it != errnoMap.end()) {
            return it->second;
        } else {
            static const std::string unknown("UNKNOWN");
            return unknown;
        }
    }
}
