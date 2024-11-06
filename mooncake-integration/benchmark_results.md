# Benchmark performance on NVIDIA A10
## Large prefill length with low request rate: input_len=1024, qps = 2
| Backend/Setting                                              | Successful Requests | Duration (s) | Total Input Tokens | Total Generated Tokens | Req Throughput (req/s) | Output Token Throughput (tok/s) | Total Token Throughput (tok/s) | Mean TTFT (ms) | Median TTFT (ms) | P99 TTFT (ms) | Mean TPOT (ms) | Median TPOT (ms) | P99 TPOT (ms) | Mean ITL (ms) | Median ITL (ms) | P99 ITL (ms) |
|--------------------------------------------------------------|---------------------|--------------|--------------------|------------------------|------------------------|---------------------------------|-------------------------------|----------------|-----------------|--------------|---------------|------------------|--------------|--------------|----------------|-------------|
| Non-disaggregated with `--enable-chunked-prefill`            | 200                 | 99.19        | 202314             | 1200                   | 2.02                   | 12.10                           | 2051.66                      | 429.37         | 286.09           | 1166.17      | 56.57         | 34.80            | 135.60       | 56.55         | 12.61           | 157.11      |
| Disaggregated P/D demo with MooncakeTransferEngine RDMA      | 200                 | 99.46        | 202314             | 1200                   | 2.01                   | 12.07                           | 2046.28                      | 1272.55        | 743.54           | 5156.62      | 52.74         | 12.46            | 524.23       | 52.60         | 12.05           | 1136.04     |
| Disaggregated P/D demo with MooncakeTransferEngine TCP       | 200                 | 99.49        | 202314             | 1200                   | 2.01                   | 12.06                           | 2045.51                      | 1925.52        | 1011.58          | 8149.52      | 100.98        | 13.64            | 957.56       | 100.73        | 12.42           | 2536.78     |

 - Non-disaggregated demo does not need to perform inter-node communication.

## Short prefill length with varying request rates: input_len=256, qps=2, 4, 6, 8
| QPS | Backend/Setting                                               | Successful Requests | Duration (s) | Total Input Tokens | Total Generated Tokens | Req Throughput (req/s) | Output Token Throughput (tok/s) | Total Token Throughput (tok/s) | Mean TTFT (ms) | Median TTFT (ms) | P99 TTFT (ms) | Mean TPOT (ms) | Median TPOT (ms) | P99 TPOT (ms) | Mean ITL (ms) | Median ITL (ms) | P99 ITL (ms) |
|-----|---------------------------------------------------------------|---------------------|--------------|--------------------|------------------------|------------------------|---------------------------------|-------------------------------|----------------|-----------------|--------------|---------------|------------------|--------------|--------------|----------------|-------------|
| 2   | Non-disaggregated with `--enable-chunked-prefill`             | 200                 | 98.72        | 49054              | 1200                   | 2.03                   | 12.16                           | 509.05                        | 64.55          | 56.19           | 136.14       | 14.29         | 12.20            | 34.41        | 14.27         | 12.21           | 45.76       |
| 2   | Disaggregated P/D demo with MooncakeTransferEngine RDMA       | 200                 | 98.70        | 49054              | 1200                   | 2.03                   | 12.16                           | 509.14                        | 103.54         | 89.44           | 223.15       | 12.02         | 11.49            | 16.28        | 11.98         | 11.46           | 30.30       |
| 2   | Disaggregated P/D demo with MooncakeTransferEngine TCP        | 200                 | 98.71        | 49054              | 1200                   | 2.03                   | 12.16                           | 509.09                        | 111.86         | 95.78           | 241.02       | 12.14         | 11.45            | 17.66        | 12.11         | 11.44           | 36.33       |
| 4   | Non-disaggregated with `--enable-chunked-prefill`             | 200                 | 49.45        | 49054              | 1200                   | 4.04                   | 24.27                           | 1016.31                       | 76.65          | 65.66           | 185.25       | 17.46         | 12.25            | 58.56        | 17.44         | 12.27           | 69.81       |
| 4   | Disaggregated P/D demo with MooncakeTransferEngine RDMA       | 200                 | 49.48        | 49054              | 1200                   | 4.04                   | 24.25                           | 1015.58                       | 122.52         | 91.97           | 300.48       | 13.30         | 11.52            | 40.96        | 13.27         | 11.48           | 54.24       |
| 4   | Disaggregated P/D demo with MooncakeTransferEngine TCP        | 200                 | 49.50        | 49054              | 1200                   | 4.04                   | 24.24                           | 1015.29                       | 132.38         | 99.81           | 329.00       | 14.50         | 11.51            | 77.32        | 14.47         | 11.49           | 77.08       |
| 6   | Non-disaggregated with `--enable-chunked-prefill`             | 200                 | 33.10        | 49054              | 1200                   | 6.04                   | 36.25                           | 1518.19                       | 92.19          | 76.48           | 253.62       | 22.06         | 16.43            | 77.50        | 22.04         | 12.34           | 127.60      |
| 6   | Disaggregated P/D demo with MooncakeTransferEngine RDMA       | 200                 | 33.15        | 49054              | 1200                   | 6.03                   | 36.20                           | 1515.84                       | 155.67         | 118.36          | 629.47       | 16.28         | 11.62            | 57.13        | 16.23         | 11.53           | 116.64      |
| 6   | Disaggregated P/D demo with MooncakeTransferEngine TCP        | 200                 | 33.17        | 49054              | 1200                   | 6.03                   | 36.18                           | 1515.03                       | 173.83         | 134.61          | 692.35       | 19.74         | 11.62            | 99.98        | 19.69         | 11.53           | 140.50      |
| 8   | Non-disaggregated with `--enable-chunked-prefill`             | 200                 | 24.93        | 49054              | 1200                   | 8.02                   | 48.13                           | 2015.49                       | 120.73         | 91.26           | 340.52       | 33.93         | 22.25            | 131.26       | 33.91         | 12.56           | 151.47      |
| 8   | Disaggregated P/D demo with MooncakeTransferEngine RDMA       | 200                 | 25.00        | 49054              | 1200                   | 8.00                   | 48.01                           | 2010.42                       | 238.67         | 154.85          | 937.64       | 24.58         | 12.26            | 172.75       | 24.51         | 11.61           | 301.37      |
| 8   | Disaggregated P/D demo with MooncakeTransferEngine TCP        | 200                 | 25.02        | 49054              | 1200                   | 8.00                   | 47.97                           | 2008.92                       | 282.11         | 170.38          | 1168.48      | 25.57         | 12.43            | 211.11       | 25.48         | 11.69           | 415.90      |

# Raw benchmark results
## Large prefill length with low request rate: input_len=1024, qps = 2
### Non-disaggregated with `--enable-chunked-prefill` enabled
```
+ default_output_len=6
+ export VLLM_LOGGING_LEVEL=DEBUG
+ VLLM_LOGGING_LEVEL=DEBUG
++ hostname -I
++ awk '{print $1}'
+ export VLLM_HOST_IP=192.168.0.137
+ VLLM_HOST_IP=192.168.0.137
+ for qps in 2
+ benchmark 2 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=2
+ prefix_len=50
+ input_len=1024
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 1024 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-2.json --request-rate 2
WARNING 11-06 15:06:13 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=2.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-2.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=1024, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 2.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [01:39<00:00,  2.02it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  99.19     
Total input tokens:                      202314    
Total generated tokens:                  1200      
Request throughput (req/s):              2.02      
Output token throughput (tok/s):         12.10     
Total Token throughput (tok/s):          2051.66   
---------------Time to First Token----------------
Mean TTFT (ms):                          429.37    
Median TTFT (ms):                        286.09    
P99 TTFT (ms):                           1166.17   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          56.57     
Median TPOT (ms):                        34.80     
P99 TPOT (ms):                           135.60    
---------------Inter-token Latency----------------
Mean ITL (ms):                           56.55     
Median ITL (ms):                         12.61     
P99 ITL (ms):                            157.11    
==================================================
```

### Disaggregated P/D demo with MooncakeTransferEngine RDMA backend
```
+ default_output_len=6
+ export VLLM_LOGGING_LEVEL=DEBUG
+ VLLM_LOGGING_LEVEL=DEBUG
++ hostname -I
++ awk '{print $1}'
+ export VLLM_HOST_IP=192.168.0.137
+ VLLM_HOST_IP=192.168.0.137
+ for qps in 2 4
+ benchmark 2 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=2
+ prefix_len=50
+ input_len=1024
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 1024 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-2.json --request-rate 2
WARNING 11-06 14:46:29 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=2.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-2.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=1024, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 2.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [01:39<00:00,  2.01it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  99.46     
Total input tokens:                      202314    
Total generated tokens:                  1200      
Request throughput (req/s):              2.01      
Output token throughput (tok/s):         12.07     
Total Token throughput (tok/s):          2046.28   
---------------Time to First Token----------------
Mean TTFT (ms):                          1272.55   
Median TTFT (ms):                        743.54    
P99 TTFT (ms):                           5156.62   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          52.74     
Median TPOT (ms):                        12.46     
P99 TPOT (ms):                           524.23    
---------------Inter-token Latency----------------
Mean ITL (ms):                           52.60     
Median ITL (ms):                         12.05     
P99 ITL (ms):                            1136.04   
==================================================
```

### Disaggregated P/D demo with MooncakeTransferEngine TCP backend
```
+ default_output_len=6
+ export VLLM_LOGGING_LEVEL=DEBUG
+ VLLM_LOGGING_LEVEL=DEBUG
++ hostname -I
++ awk '{print $1}'
+ export VLLM_HOST_IP=192.168.0.137
+ VLLM_HOST_IP=192.168.0.137
+ for qps in 2
+ benchmark 2 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=2
+ prefix_len=50
+ input_len=1024
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 1024 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-2.json --request-rate 2
WARNING 11-06 14:52:00 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=2.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-2.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=1024, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 2.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [01:39<00:00,  2.01it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  99.49     
Total input tokens:                      202314    
Total generated tokens:                  1200      
Request throughput (req/s):              2.01      
Output token throughput (tok/s):         12.06     
Total Token throughput (tok/s):          2045.51   
---------------Time to First Token----------------
Mean TTFT (ms):                          1925.52   
Median TTFT (ms):                        1011.58   
P99 TTFT (ms):                           8149.52   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          100.98    
Median TPOT (ms):                        13.64     
P99 TPOT (ms):                           957.56    
---------------Inter-token Latency----------------
Mean ITL (ms):                           100.73    
Median ITL (ms):                         12.42     
P99 ITL (ms):                            2536.78   
==================================================
```

## Short prefill length with varing request rates: input_len=256, qps = 2, 4, 6, 8

### Non-disaggregated with `--enable-chunked-prefill` enabled
```
+ default_output_len=6
+ export VLLM_LOGGING_LEVEL=DEBUG
+ VLLM_LOGGING_LEVEL=DEBUG
++ hostname -I
++ awk '{print $1}'
+ export VLLM_HOST_IP=192.168.0.137
+ VLLM_HOST_IP=192.168.0.137
+ for qps in 2 4 6 8
+ benchmark 2 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=2
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-2.json --request-rate 2
WARNING 11-06 15:10:16 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=2.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-2.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 2.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [01:38<00:00,  2.03it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  98.72     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              2.03      
Output token throughput (tok/s):         12.16     
Total Token throughput (tok/s):          509.05    
---------------Time to First Token----------------
Mean TTFT (ms):                          64.55     
Median TTFT (ms):                        56.19     
P99 TTFT (ms):                           136.14    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          14.29     
Median TPOT (ms):                        12.20     
P99 TPOT (ms):                           34.41     
---------------Inter-token Latency----------------
Mean ITL (ms):                           14.27     
Median ITL (ms):                         12.21     
P99 ITL (ms):                            45.76     
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 4 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=4
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-4.json --request-rate 4
WARNING 11-06 15:12:09 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=4.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-4.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 4.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:49<00:00,  4.04it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  49.45     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              4.04      
Output token throughput (tok/s):         24.27     
Total Token throughput (tok/s):          1016.31   
---------------Time to First Token----------------
Mean TTFT (ms):                          76.65     
Median TTFT (ms):                        65.66     
P99 TTFT (ms):                           185.25    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          17.46     
Median TPOT (ms):                        12.25     
P99 TPOT (ms):                           58.56     
---------------Inter-token Latency----------------
Mean ITL (ms):                           17.44     
Median ITL (ms):                         12.27     
P99 ITL (ms):                            69.81     
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 6 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=6
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-6.json --request-rate 6
WARNING 11-06 15:13:13 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=6.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-6.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 6.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:33<00:00,  6.04it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  33.10     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              6.04      
Output token throughput (tok/s):         36.25     
Total Token throughput (tok/s):          1518.19   
---------------Time to First Token----------------
Mean TTFT (ms):                          92.19     
Median TTFT (ms):                        76.48     
P99 TTFT (ms):                           253.62    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          22.06     
Median TPOT (ms):                        16.43     
P99 TPOT (ms):                           77.50     
---------------Inter-token Latency----------------
Mean ITL (ms):                           22.04     
Median ITL (ms):                         12.34     
P99 ITL (ms):                            127.60    
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 8 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=8
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-8.json --request-rate 8
WARNING 11-06 15:14:00 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=8.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-8.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 8.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:24<00:00,  8.02it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  24.93     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              8.02      
Output token throughput (tok/s):         48.13     
Total Token throughput (tok/s):          2015.49   
---------------Time to First Token----------------
Mean TTFT (ms):                          120.73    
Median TTFT (ms):                        91.26     
P99 TTFT (ms):                           340.52    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          33.93     
Median TPOT (ms):                        22.25     
P99 TPOT (ms):                           131.26    
---------------Inter-token Latency----------------
Mean ITL (ms):                           33.91     
Median ITL (ms):                         12.56     
P99 ITL (ms):                            151.47    
==================================================
+ sleep 10
```


### Disaggregated P/D demo with MooncakeTransferEngine RDMA backend
```
+ export VLLM_HOST_IP=192.168.0.137
+ VLLM_HOST_IP=192.168.0.137
+ for qps in 2 4 6 8
+ benchmark 2 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=2
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-2.json --request-rate 2
WARNING 11-06 14:28:17 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=2.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-2.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 2.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [01:38<00:00,  2.03it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  98.70     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              2.03      
Output token throughput (tok/s):         12.16     
Total Token throughput (tok/s):          509.14    
---------------Time to First Token----------------
Mean TTFT (ms):                          103.54    
Median TTFT (ms):                        89.44     
P99 TTFT (ms):                           223.15    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          12.02     
Median TPOT (ms):                        11.49     
P99 TPOT (ms):                           16.28     
---------------Inter-token Latency----------------
Mean ITL (ms):                           11.98     
Median ITL (ms):                         11.46     
P99 ITL (ms):                            30.30     
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 4 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=4
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-4.json --request-rate 4
WARNING 11-06 14:30:14 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=4.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-4.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 4.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:49<00:00,  4.04it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  49.48     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              4.04      
Output token throughput (tok/s):         24.25     
Total Token throughput (tok/s):          1015.58   
---------------Time to First Token----------------
Mean TTFT (ms):                          122.52    
Median TTFT (ms):                        91.97     
P99 TTFT (ms):                           300.48    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          13.30     
Median TPOT (ms):                        11.52     
P99 TPOT (ms):                           40.96     
---------------Inter-token Latency----------------
Mean ITL (ms):                           13.27     
Median ITL (ms):                         11.48     
P99 ITL (ms):                            54.24     
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 6 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=6
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-6.json --request-rate 6
WARNING 11-06 14:31:18 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=6.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-6.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 6.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:33<00:00,  6.03it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  33.15     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              6.03      
Output token throughput (tok/s):         36.20     
Total Token throughput (tok/s):          1515.84   
---------------Time to First Token----------------
Mean TTFT (ms):                          155.67    
Median TTFT (ms):                        118.36    
P99 TTFT (ms):                           629.47    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          16.28     
Median TPOT (ms):                        11.62     
P99 TPOT (ms):                           57.13     
---------------Inter-token Latency----------------
Mean ITL (ms):                           16.23     
Median ITL (ms):                         11.53     
P99 ITL (ms):                            116.64    
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 8 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=8
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-8.json --request-rate 8
WARNING 11-06 14:32:05 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=8.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-8.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 8.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:24<00:00,  8.00it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  25.00     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              8.00      
Output token throughput (tok/s):         48.01     
Total Token throughput (tok/s):          2010.42   
---------------Time to First Token----------------
Mean TTFT (ms):                          238.67    
Median TTFT (ms):                        154.85    
P99 TTFT (ms):                           937.64    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          24.58     
Median TPOT (ms):                        12.26     
P99 TPOT (ms):                           172.75    
---------------Inter-token Latency----------------
Mean ITL (ms):                           24.51     
Median ITL (ms):                         11.61     
P99 ITL (ms):                            301.37    
==================================================
+ sleep 10
```

### Disaggregated P/D demo with MooncakeTransferEngine TCP backend
```
+ export VLLM_HOST_IP=192.168.0.137
+ VLLM_HOST_IP=192.168.0.137
+ for qps in 2 4 6 8
+ benchmark 2 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=2
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-2.json --request-rate 2
WARNING 11-06 13:51:12 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=2.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-2.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 2.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [01:38<00:00,  2.03it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  98.71     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              2.03      
Output token throughput (tok/s):         12.16     
Total Token throughput (tok/s):          509.09    
---------------Time to First Token----------------
Mean TTFT (ms):                          111.86    
Median TTFT (ms):                        95.78     
P99 TTFT (ms):                           241.02    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          12.14     
Median TPOT (ms):                        11.45     
P99 TPOT (ms):                           17.66     
---------------Inter-token Latency----------------
Mean ITL (ms):                           12.11     
Median ITL (ms):                         11.44     
P99 ITL (ms):                            36.33     
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 4 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=4
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-4.json --request-rate 4
WARNING 11-06 13:53:05 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=4.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-4.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 4.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:49<00:00,  4.04it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  49.50     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              4.04      
Output token throughput (tok/s):         24.24     
Total Token throughput (tok/s):          1015.29   
---------------Time to First Token----------------
Mean TTFT (ms):                          132.38    
Median TTFT (ms):                        99.81     
P99 TTFT (ms):                           329.00    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          14.50     
Median TPOT (ms):                        11.51     
P99 TPOT (ms):                           77.32     
---------------Inter-token Latency----------------
Mean ITL (ms):                           14.47     
Median ITL (ms):                         11.49     
P99 ITL (ms):                            77.08     
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 6 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=6
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-6.json --request-rate 6
WARNING 11-06 13:54:09 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=6.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-6.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 6.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:33<00:00,  6.03it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  33.17     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              6.03      
Output token throughput (tok/s):         36.18     
Total Token throughput (tok/s):          1515.03   
---------------Time to First Token----------------
Mean TTFT (ms):                          173.83    
Median TTFT (ms):                        134.61    
P99 TTFT (ms):                           692.35    
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          19.74     
Median TPOT (ms):                        11.62     
P99 TPOT (ms):                           99.98     
---------------Inter-token Latency----------------
Mean ITL (ms):                           19.69     
Median ITL (ms):                         11.53     
P99 ITL (ms):                            140.50    
==================================================
+ sleep 10
+ for qps in 2 4 6 8
+ benchmark 8 6 disagg_prefill
+ results_folder=./results
+ model=Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4
+ dataset_name=sonnet
+ dataset_path=../sonnet_4x.txt
+ num_prompts=200
+ qps=8
+ prefix_len=50
+ input_len=256
+ output_len=6
+ tag=disagg_prefill
+ python3 ../benchmark_serving.py --backend vllm --model Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4 --dataset-name sonnet --dataset-path ../sonnet_4x.txt --sonnet-input-len 256 --sonnet-output-len 6 --sonnet-prefix-len 50 --num-prompts 200 --port 8000 --save-result --result-dir ./results --result-filename disagg_prefill-qps-8.json --request-rate 8
WARNING 11-06 13:54:56 cuda.py:22] You are using a deprecated `pynvml` package. Please install `nvidia-ml-py` instead, and make sure to uninstall `pynvml`. When both of them are installed, `pynvml` will take precedence and cause errors. See https://pypi.org/project/pynvml for more information.
Namespace(backend='vllm', base_url=None, host='localhost', port=8000, endpoint='/v1/completions', dataset=None, dataset_name='sonnet', dataset_path='../sonnet_4x.txt', model='Qwen/Qwen2.5-7B-Instruct-GPTQ-Int4', tokenizer=None, best_of=1, use_beam_search=False, num_prompts=200, logprobs=None, request_rate=8.0, seed=0, trust_remote_code=False, disable_tqdm=False, profile=False, save_result=True, metadata=None, result_dir='./results', result_filename='disagg_prefill-qps-8.json', ignore_eos=False, percentile_metrics='ttft,tpot,itl', metric_percentiles='99', sonnet_input_len=256, sonnet_output_len=6, sonnet_prefix_len=50, sharegpt_output_len=None, random_input_len=1024, random_output_len=128, random_range_ratio=1.0, random_prefix_len=0, hf_subset=None, hf_split=None, hf_output_len=None)
Starting initial single prompt test run...
Initial test run completed. Starting main benchmark run...
Traffic request rate: 8.0
100%|███████████████████████████████████████████████████████████████████████████| 200/200 [00:25<00:00,  8.00it/s]
============ Serving Benchmark Result ============
Successful requests:                     200       
Benchmark duration (s):                  25.02     
Total input tokens:                      49054     
Total generated tokens:                  1200      
Request throughput (req/s):              8.00      
Output token throughput (tok/s):         47.97     
Total Token throughput (tok/s):          2008.92   
---------------Time to First Token----------------
Mean TTFT (ms):                          282.11    
Median TTFT (ms):                        170.38    
P99 TTFT (ms):                           1168.48   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          25.57     
Median TPOT (ms):                        12.43     
P99 TPOT (ms):                           211.11    
---------------Inter-token Latency----------------
Mean ITL (ms):                           25.48     
Median ITL (ms):                         11.69     
P99 ITL (ms):                            415.90    
==================================================
+ sleep 10
```
