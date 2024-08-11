// transfer_engine_c.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/transfer_engine_c.h"
#include "transfer_engine/multi_transfer_engine.h"
#include "transfer_engine/transport.h"


using namespace mooncake;

transfer_engine_t createTransferEngine() {
    MultiTransferEngine *native = new MultiTransferEngine();
    return (transfer_engine_t) native;
}

transport_t installTransport(transfer_engine_t engine, const char *proto, const char *path_prefix, void **args) {
    MultiTransferEngine *native = (MultiTransferEngine *) engine;
    return (transport_t)native->installTransport(proto, path_prefix, args);
}

int uninstallTransport(transfer_engine_t engine, transport_t transport) {
    MultiTransferEngine *native = (MultiTransferEngine *) engine;
    return native->uninstallTransport((Transport *)transport);
}

void destroyTransferEngine(transfer_engine_t engine)
{
    MultiTransferEngine *native = (MultiTransferEngine *)engine;
    delete native;
}

segment_id_t openSegment(transport_t xport, const char *segment_name)
{
    Transport *native = (Transport *)xport;
    return (segment_id_t)native->openSegment(segment_name);
}

int closeSegment(transport_t xport, segment_id_t segment_id)
{
    Transport *native = (Transport *)xport;
    return native->closeSegment(segment_id);
}

int registerLocalMemory(transport_t xport, void *addr, size_t length, const char *location)
{
    Transport *native = (Transport *)xport;
    return native->registerLocalMemory(addr, length, location);
}

int unregisterLocalMemory(transport_t xport, void *addr)
{
    Transport *native = (Transport *)xport;
    return native->unregisterLocalMemory(addr);
}

int registerLocalMemoryBatch(transfer_engine_t engine, buffer_entry_t *buffer_list, size_t buffer_len, const char *location)
{
    TransferEngine *native = (TransferEngine *)engine;
    std::vector<TransferEngine::BufferEntry> native_buffer_list;
    for (size_t i = 0; i < buffer_len; ++i)
    {
        TransferEngine::BufferEntry entry;
        entry.addr = buffer_list[i].addr;
        entry.length = buffer_list[i].length;
        native_buffer_list.push_back(entry);
    }
    return native->registerLocalMemoryBatch(native_buffer_list, location);
}

int unregisterLocalMemoryBatch(transfer_engine_t engine, void **addr_list, size_t addr_len)
{
    TransferEngine *native = (TransferEngine *)engine;
    std::vector<void *> native_addr_list;
    for (size_t i = 0; i < addr_len; ++i)
        native_addr_list.push_back(addr_list[i]);
    return native->unregisterLocalMemoryBatch(native_addr_list);
}

batch_id_t allocateBatchID(transfer_engine_t engine, size_t batch_size)
{
    TransferEngine *native = (TransferEngine *)engine;
    return (batch_id_t)native->allocateBatchID(batch_size);
}

int submitTransfer(transport_t xport, 
                   batch_id_t batch_id,
                   struct transfer_request *entries,
                   size_t count)
{
    Transport *native = (Transport *)xport;
    std::vector<Transport::TransferRequest> native_entries;
    native_entries.resize(count);
    for (size_t index = 0; index < count; index++)
    {
        native_entries[index].opcode = (Transport::TransferRequest::OpCode)entries[index].opcode;
        native_entries[index].source = entries[index].source;
        native_entries[index].target_id = entries[index].target_id;
        native_entries[index].target_offset = entries[index].target_offset;
        native_entries[index].length = entries[index].length;
    }
    return native->submitTransfer((BatchID)batch_id, native_entries);
}

int getTransferStatus(transport_t xport, 
                      batch_id_t batch_id,
                      size_t task_id,
                      struct transfer_status *status)
{
    Transport *native = (Transport *)xport;
    Transport::TransferStatus native_status;
    int rc = native->getTransferStatus((BatchID) batch_id, task_id, native_status);
    if (rc == 0) {
        status->status = (int) native_status.s;
        status->transferred_bytes = native_status.transferred_bytes;
    }
    return rc;
}

int freeBatchID(transport_t xport, batch_id_t batch_id)
{
    Transport *native = (Transport *)xport;
    return native->freeBatchID(batch_id);
}



// transfer_engine_t createTransferEngine(const char *metadata_uri, 
//                                        const char *local_server_name,
//                                        const char *nic_priority_matrix)
// {
//     auto metadata_client = std::make_unique<TransferMetadata>(metadata_uri);
//     TransferEngine *native = new TransferEngine(
//         metadata_client, local_server_name, nic_priority_matrix);
//     return (transfer_engine_t) native;
// }

// void destroyTransferEngine(transfer_engine_t engine)
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     delete native;
// }

// int registerLocalMemory(transfer_engine_t engine, void *addr, size_t length, const char *location)
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     return native->registerLocalMemory(addr, length, location);
// }

// int unregisterLocalMemory(transfer_engine_t engine, void *addr)
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     return native->unregisterLocalMemory(addr);
// }

// batch_id_t allocateBatchID(transfer_engine_t engine, size_t batch_size)
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     return (batch_id_t) native->allocateBatchID(batch_size);
// }

// int submitTransfer(transfer_engine_t engine, 
//                    batch_id_t batch_id,
//                    struct transfer_request *entries,
//                    size_t count) 
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     std::vector<TransferRequest> native_entries;
//     native_entries.resize(count);
//     for (size_t index = 0; index < count; index++)
//     {
//         native_entries[index].opcode = (TransferRequest::OpCode) entries[index].opcode;
//         native_entries[index].source = entries[index].source;
//         native_entries[index].target_id = entries[index].target_id;
//         native_entries[index].target_offset = entries[index].target_offset;
//         native_entries[index].length = entries[index].length;
//     }
//     return native->submitTransfer((BatchID) batch_id, native_entries);
// }

// int getTransferStatus(transfer_engine_t engine, 
//                       batch_id_t batch_id,
//                       size_t task_id,
//                       struct transfer_status *status) 
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     TransferStatus native_status;
//     int rc = native->getTransferStatus((BatchID) batch_id, task_id, native_status);
//     if (rc == 0) {
//         status->status = (int) native_status.s;
//         status->transferred_bytes = native_status.transferred_bytes;
//     }
//     return rc;
// }

// int freeBatchID(transfer_engine_t engine, batch_id_t batch_id)
// {
//     TransferEngine *native = (TransferEngine *) engine;
//     return native->freeBatchID(batch_id);
// }
