# P2P Store

P2P Store is built on [Transfer Engine](transfer-engine.md) and supports the temporary sharing of objects between peer nodes in a cluster, with typical scenarios including checkpoint distribution, etc. P2P Store is a pure client architecture with no centralized master node; global metadata is maintained by the etcd service. P2P Store is now used in Moonshot AI's checkpoint transfer service.

P2P Store provides interfaces similar to Register and GetReplica. Register is equivalent to seeding in BT, where a local file is registered with the global metadata without any data transfer occurring; it merely registers metadata. The GetReplica interface searches metadata and clones data from other machines that have called Register or Get (unless explicitly calling Unregister or DeleteReplica to stop pulling files from the local machine), and it can also act as a data source to improve the efficiency of data transfer for other nodes. This approach can increase the efficiency of large-scale data distribution and avoid the impact of single-machine export bandwidth saturation on distribution efficiency.

## P2P Store Demonstration Program
After compiling P2P Store successfully by following the compilation guide with `cmake .. -DWITH_P2P_STORE=ON && make -j`, a test program `p2p-store-example` will be produced in the `build/mooncake-p2p-store` directory. This tool demonstrates the usage of P2P Store, simulating the process of migrating model data from training nodes to a large number of inference nodes after the training task is completed. Currently, it only supports the RDMA protocol.

1. **Start the `etcd` service.** This is consistent with the method described in Transfer Engine Bench.

2. **Start the simulated training node.** This node will create a simulated model file and make it public within the cluster.
   ```bash
   # This is 10.0.0.2
   export MC_GID_INDEX=n    # NOTE that n is integer
   ./p2p-store-example --cmd=trainer \
                       --metadata_server=10.0.0.1:2379 \
                       --local_server_name=10.0.0.2:12345 \
                       --device_name=erdma_0
   ```

3. **Start the simulated inference node.** This node will pull data from the simulated training node or other simulated inference nodes.
   ```bash
   # This is 10.0.0.3
   export MC_GID_INDEX=n
   ./p2p-store-example --cmd=inferencer \
                       --metadata_server=10.0.0.1:2379 \
                       --local_server_name=10.0.0.3:12346 \
                       --device_name=erdma_1
   ```
   The test is completed with the display of "ALL DONE".

In the above process, the simulated inference nodes search for data sources, which is implemented by the internal logic of P2P Store, so there is no need for users to provide the IP of the training node. Similarly, it is necessary to ensure that other nodes can access this machine using the local machine's hostname `hostname(2)` or the `--local_server_name` filled in during the creation of the node.
