#!/usr/bin/env python3
import threading
from vllm_adaptor import MooncakeTransfer

def alloc_free_task(length, mc):
    ptr_list = []
    for i in range(1024):
        ptr = mc.allocate_managed_buffer(length)
        ptr_list.append(ptr)
    
    for ptr in ptr_list:
        mc.free_managed_buffer(ptr, length)

# 设置线程数量和内存长度
thread_count = 10
length = 1024
mc = MooncakeTransfer()
mc.initialize("192.168.0.137:10002", "192.168.0.139:2379", "rdma", "erdma_0")

# 创建线程列表
threads = []

# 创建并启动线程
for _ in range(thread_count):
    thread = threading.Thread(target=alloc_free_task, args=(length, mc))
    threads.append(thread)
    thread.start()

# 等待所有线程完成
for thread in threads:
    thread.join()
