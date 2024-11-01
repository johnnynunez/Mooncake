* Server Side

```python
mc = MooncakeTransfer()
mq = MessageQueue()
mc.initialize("local IP:port", "192.168.0.139:2379", "rdma", "erdma_0")
ptr = mc.allocate_managed_buffer(length)
# Transfer ptr to client side
time.sleep(100)
mc.free_managed_buffer(ptr, length)
```

* Client Side

```python
mc = MooncakeTransfer()
mq = MessageQueue()
mc.initialize("local IP:port", "192.168.0.139:2379", "rdma", "erdma_0")
ptr = mc.allocate_managed_buffer(length)
ptr_dst = # Remote ptr in server side
mc.transfer_sync("192.168.0.138:10001", ptr, ptr_dst, length)
mc.free_managed_buffer(ptr, length)
```