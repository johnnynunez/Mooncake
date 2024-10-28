import mooncake_vllm_adaptor as mva
import pika

class MooncakeTransfer:
    def __init__(self):
        self.mva_ins = mva.mooncake_vllm_adaptor()
    
    def initialize(self, local_hostname: str, metadata_server: str, protocol: str, device_name: str):
        return self.mva_ins.initialize(local_hostname, metadata_server, protocol, device_name)

    def allocate_managed_buffer(self, length: int) -> int:
        return self.mva_ins.allocateManagedBuffer(length)
    
    def free_managed_buffer(self, buffer: int, length: int) -> int:
        return self.mva_ins.freeManagedBuffer(buffer, length)
    
    def transfer_sync(self, target_hostname: str, buffer: int, peer_buffer_address: int, length: int) -> int:
        return self.mva_ins.transferSync(target_hostname, buffer, peer_buffer_address, length)

class MessageQueue:
    ptr: int = 0
    def __init__(self):
        self.connection = pika.BlockingConnection(pika.ConnectionParameters(host='localhost'))
        self.channel = self.connection.channel()
        self.channel.queue_declare(queue='ptr')
    
    def send_ptr(self, ptr: int):
        self.channel.basic_publish(exchange='', routing_key='hello', body=str(ptr))

    def recv_ptr(self):
        def callback(ch, method, properties, body):
            self.ptr = int(body.decode())
        self.channel.basic_consume(queue='ptr', on_message_callback=callback, auto_ack=True)

        self.channel.start_consuming()
        print(self.ptr)
        return self.ptr
        