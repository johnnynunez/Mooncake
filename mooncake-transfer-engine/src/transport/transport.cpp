#include "transport/transport.h"
#include "error.h"
#include "transfer_engine.h"

namespace mooncake
{
    Transport::BatchID Transport::allocateBatchID(size_t batch_size)
    {
        auto batch_desc = new BatchDesc();
        if (!batch_desc)
            return -1;
        batch_desc->id = BatchID(batch_desc);
        batch_desc->batch_size = batch_size;
        batch_desc->task_list.reserve(batch_size);
        batch_desc->context = NULL;
#ifdef CONFIG_USE_BATCH_DESC_SET
        batch_desc_lock_.lock();
        batch_desc_set_[batch_desc->id] = batch_desc;
        batch_desc_lock_.unlock();
#endif
        return batch_desc->id;
    }

    int Transport::freeBatchID(BatchID batch_id)
    {
        auto &batch_desc = *((BatchDesc *)(batch_id));
        const size_t task_count = batch_desc.task_list.size();
        for (size_t task_id = 0; task_id < task_count; task_id++)
        {
            if (!batch_desc.task_list[task_id].is_finished)
            {
                LOG(ERROR) << "BatchID cannot be freed until all tasks are done";
                return -1;
            }
        }
        delete &batch_desc;
#ifdef CONFIG_USE_BATCH_DESC_SET
        RWSpinlock::WriteGuard guard(batch_desc_lock_);
        batch_desc_set_.erase(batch_id);
#endif
        return 0;
    }

    int Transport::install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args)
    {
        local_server_name_ = local_server_name;
        metadata_ = meta;
        return 0;
    }
}