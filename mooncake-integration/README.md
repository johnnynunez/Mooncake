### vLLM Disaggregated Prefill/Decode Demo
Currently, we support mooncake-transfer-engine integration with the vLLM project based on https://github.com/vllm-project/vllm/pull/8498 to accelerate KVCache transfer for inter-node disaggregated Prefill/Decode scenario ([Benchmark results](benchmark_results.md)). In the future, we will bypass PR 8498, release a disaggregated KVStore, and fully integrate it with the vLLM Prefix Caching feature to support multi-instance KVCache Sharing.

#### Prepare configuration file to Run Example over RDMA

- Prepare a _**mooncake.json**_ file for both Prefill and Decode instances
```json
{
  "local_url": "192.168.0.137:13003",
  "remote_url": "192.168.0.139:13003",
  "metadata_server": "192.168.0.139:2379",
  "protocol": "rdma",
  "device_name": "erdma_0"
}
```
- "local_url": The IP address and port of the Prefill node.
  - The port in the URL is used to communicate with etcd server for metadata.
- "remote_url": The IP address and port of the Decode node.
  - The port in the URL is used to communicate with etcd server for metadata.
- "metadata_server": The etcd server of mooncake transfer engine.
- "protocol": The protocol to be used for data transmission. ("rdma/tcp")
- "device_name": The device to be used for data transmission, required when "protocol" set to "rdma".


#### Prepare configuration file to Run Example over TCP

- Prepare a _**mooncake.json**_ file for both Prefill and Decode instances
```json
{
  "local_url": "192.168.0.137:13003",
  "remote_url": "192.168.0.139:13003",
  "metadata_server": "192.168.0.139:2379",
  "protocol": "tcp",
  "device_name": ""
}
```


#### Run Example
```bash
# Begin from `root` of your cloned repo!

# 1. Start the etcd server
etcd --listen-client-urls http://0.0.0.0:2379 --advertise-client-urls http://localhost:2379
# You may need to terminate other etcd processes before running the above command

# 2. Configuration
export VLLM_PORT=51000  # Need to set this up for both Prefill and Decode instances on different nodes using same port

# 3. Run on the prefill side
MASTER_ADDR="192.168.0.137" MASTER_PORT="54324" MC_GID_INDEX=1 MOONCAKE_CONFIG_PATH=./mooncake.json VLLM_DISTRIBUTED_KV_ROLE=producer VLLM_USE_MODELSCOPE=True python3 -m vllm.entrypoints.openai.api_server --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --port 8100 --max-model-len 10000 --gpu-memory-utilization 0.95

# 4. Run on the decode side
MASTER_ADDR="192.168.0.137" MASTER_PORT="54324" MC_GID_INDEX=1 MOONCAKE_CONFIG_PATH=./mooncake.json VLLM_DISTRIBUTED_KV_ROLE=consumer VLLM_USE_MODELSCOPE=True python3 -m vllm.entrypoints.openai.api_server --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --port 8200 --max-model-len 10000 --gpu-memory-utilization 0.95
```

 - **_Be sure to set up the same MASTER_ADDR and same MASTER_PORT on each node (either prefill instance IP or decode instance IP is ok)._**
- MASTER_PORT is used for inter-node torch setup communication.
- MC_GID_INDEX is the gid of the target rdma device.
- MOONCAKE_CONFIG_PATH is the path to the mooncake.json configuration file.
- VLLM_DISTRIBUTED_KV_ROLE is the node's role, either 'producer' or 'consumer'.
- VLLM_USE_MODELSCOPE is optional, if you have access to huggingface, please remove it.
- The `--model` parameter specifies the model to use.
- The `--port` parameter specifies the vllm service port on which to listen.
- The `--max-model-len` parameter specifies the maximum length of the model.
- Currently, option `--tensor_parallel_size` \ `-tp` is not supported for inter-node disaggregated scenario due to the initialization process of `disagg_group` in conflict with the `process_group` of `torch.distributed`. This issue will be addressed in the next patch. Stay tuned.
```bash
# 5. Start the proxy server on one node (Let's take the prefill node as an example)
python3 proxy_server.py
```
The implementation of `proxy_server.py`
```python
import os

import aiohttp
from quart import Quart, make_response, request

AIOHTTP_TIMEOUT = aiohttp.ClientTimeout(total=6 * 60 * 60)

app = Quart(__name__)


async def forward_request(url, data):
    async with aiohttp.ClientSession(timeout=AIOHTTP_TIMEOUT) as session:
        headers = {
            "Authorization": f"Bearer {os.environ.get('OPENAI_API_KEY')}"
        }
        async with session.post(url=url, json=data,
                                headers=headers) as response:
            if response.status == 200:
                if True:
                    async for chunk_bytes in response.content.iter_chunked(
                            1024):
                        yield chunk_bytes
                else:
                    content = await response.read()
                    yield content


@app.route('/v1/completions', methods=['POST'])
async def handle_request():
    try:
        original_request_data = await request.get_json()

        prefill_request = original_request_data.copy()
        # change max_tokens = 1 to let it only do prefill
        prefill_request['max_tokens'] = 1

        # finish prefill
        async for _ in forward_request('http://localhost:8100/v1/completions',
                                       prefill_request):
            continue

        # return decode
        generator = forward_request('http://192.168.0.139:8200/v1/completions', # Be sure to change the IP address for your machine
                                    original_request_data)
        response = await make_response(generator)
        response.timeout = None

        return response

    except Exception as e:
        import sys
        import traceback
        exc_info = sys.exc_info()
        print("Error occurred in disagg prefill proxy server")
        print(e)
        print("".join(traceback.format_exception(*exc_info)))


if __name__ == '__main__':
    app.run(host="0.0.0.0",port=8000)
```

**_Be sure to change the IP address in the code_**


# 6. Test with open-ai compatible request
```
curl -s http://localhost:8000/v1/completions -H "Content-Type: application/json" -d '{
"model": "Qwen/Qwen2.5-7B-Instruct",
"prompt": "San Francisco is a",
"max_tokens": 1000,
"temperature": 0
}'
```
- If you are not testing on the proxy server, please change the `localhost` to the IP address of the proxy server.