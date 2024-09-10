#include "common_types.h"

namespace mooncake
{
    int64_t getError(ERRNO err)
    {
        return static_cast<int64_t>(err);
    }
}
