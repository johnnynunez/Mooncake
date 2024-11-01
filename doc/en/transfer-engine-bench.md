# Transfer Engine Bench

The sample program provided in `mooncake-transfer-engine/example/transfer_engine_bench.cpp` demonstrates the basic usage of Transfer Engine by repeatedly reading/writing data blocks from the DRAM of the target node to the initiator node through the Transfer Engine interface. It can also be used to measure read and write throughput. Currently, the Transfer Engine Bench tool supports RDMA and TCP protocols.

After successfully compiling Transfer Engine, the test program `transfer_engine_bench` can be found in the `build/mooncake-transfer-engine/example` directory.

1. **Start the `etcd` service.** This service is used for the centralized highly available management of various metadata for Mooncake, including the internal connection status of Transfer Engine. It is necessary to ensure that both the initiator and target nodes can smoothly access this etcd service, so pay attention to:
   - The listening IP of the etcd service should not be 127.0.0.1; it should be determined in conjunction with the network environment. In the experimental environment, 0.0.0.0 can be used. For example, the following command line can be used to start the required service:
      ```bash
      # This is 10.0.0.1
      etcd --listen-client-urls http://0.0.0.0:2379  --advertise-client-urls http://10.0.0.1:2379
      ```
   - On some platforms, if the initiator and target nodes have set the `http_proxy` or `https_proxy` environment variables, it will also affect the communication between Transfer Engine and the etcd service, reporting the "Error from etcd client: 14" error.

2. **Start the target node.**
    ```bash
    # This is 10.0.0.2
    export MC_GID_INDEX=n
    ./transfer_engine_bench --mode=target \
                            --metadata_server=10.0.0.1:2379 \
                            --local_server_name=10.0.0.2:12345 \
                            --device_name=erdma_0
    ```
   The meanings of the various parameters are as follows:
   - The default value of the environment variable `MC_GID_INDEX` is 3, which is the GID Index used by most IB/RoCE networks. For Alibaba eRDMA, it needs to be set to 1.
   - `--mode=target` indicates the start of the target node. The target node does not initiate read/write requests; it passively supplies or writes data as required by the initiator node.
      > Note: In actual applications, there is no need to distinguish between target nodes and initiator nodes; each node can freely initiate read/write requests to other nodes in the cluster.
   - `--metadata_server` is the address of the metadata server (the full address of the etcd service).
   - `--local_server_name` represents the address of this machine, which does not need to be set in most cases. If this option is not set, the value is equivalent to the hostname of this machine (i.e., `hostname(2)`). Other nodes in the cluster will use this address to attempt out-of-band communication with this node to establish RDMA connections.
      > Note: If out-of-band communication fails, the connection cannot be established. Therefore, if necessary, you need to modify the `/etc/hosts` file on all nodes in the cluster to locate the correct node through the hostname.
   - `--device_name` indicates the name of the RDMA network card used in the transfer process.
      > Tip: Advanced users can also pass in a JSON file of the network card priority matrix through `--nic_priority_matrix`, for details, refer to the developer manual of Transfer Engine.
   - In network environments that only support TCP, the `--protocol=tcp` parameter can be used; in this case, there is no need to specify the `--device_name` parameter.

1. **Start the initiator node.**
    ```bash
    # This is 10.0.0.3
    export MC_GID_INDEX=n
    ./transfer_engine_bench --metadata_server=10.0.0.1:2379 \
                            --segment_id=10.0.0.2:12345 \
                            --local_server_name=10.0.0.3:12346 \
                            --device_name=erdma_1
    ```
   The meanings of the various parameters are as follows (the rest are the same as before):
   - `--segment_id` can be simply understood as the hostname of the target node and needs to be consistent with the value passed to `--local_server_name` when starting the target node (if any).
   
   Under normal circumstances, the initiator node will start the transfer operation, wait for 10 seconds, and then display the "Test completed" message, indicating that the test is complete.

   The initiator node can also configure the following test parameters: `--operation` (can be `"read"` or `"write"`), `batch_size`, `block_size`, `duration`, `threads`, etc.

> If an exception occurs during execution, it is usually due to incorrect parameter settings. It is recommended to refer to the [troubleshooting document](troubleshooting.md) for preliminary troubleshooting.
