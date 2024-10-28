#!/usr/bin/env python3
import vllm_adaptor
from vllm_adaptor import MooncakeTransfer
from mq_wrapper import MessageQueue
from multiprocessing import Process, Queue

if __name__ == "__main__":
    length = 1024
    mc = MooncakeTransfer()
    mq = MessageQueue()
    # 第一个参数是本机的 IP 和端口，第二个参数是 etcd 服务的IP 和端口
    mc.initialize("192.168.0.137:10002", "192.168.0.139:2379", "rdma", "erdma_0")
    ptr = mc.allocate_managed_buffer(length)
    ptr_dst = mq.recv_ptr()
    print('start transfer!')
    # 第一个参数是目标服务端的 IP 和端口！
    ret = mc.transfer_sync("192.168.0.137:10001", ptr, ptr_dst, length)
    print(ret)
    data = mc.read_bytes_from_buffer(ptr, length)
    print(data)
    mc.free_managed_buffer(ptr, length)
