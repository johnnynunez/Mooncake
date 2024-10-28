#!/usr/bin/env python3
import vllm_adaptor
from vllm_adaptor import MooncakeTransfer, MessageQueue
import time
from multiprocessing import Process, Queue

if __name__ == "__main__":
    length = 1024
    mc = MooncakeTransfer()
    mq = MessageQueue()
    mc.initialize("192.168.0.138:10001", "192.168.0.139:2379", "rdma", "erdma_0")
    ptr = int(mc.allocate_managed_buffer(length))
    print(ptr)
    mq.send_ptr(ptr)
    time.sleep(100)
    mc.free_managed_buffer(ptr, length)
