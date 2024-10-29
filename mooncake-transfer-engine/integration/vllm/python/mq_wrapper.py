import pika

class MessageQueue:
    ptr: int = 0
    def __init__(self):
        self.connection = pika.BlockingConnection(pika.ConnectionParameters(host='localhost'))
        self.channel = self.connection.channel()
        self.channel.queue_declare(queue='ptr')
    
    def send_ptr(self, ptr: int):
        print("step-1")
        self.channel.basic_publish(exchange='', routing_key='ptr', body=str(ptr))
        print("step-2")

    # start_consuming 会一直 blocking，除非在 callback 主动调 stop_consuming
    def recv_ptr(self):
        def callback(ch, method, properties, body):
            print("step-2")
            self.ptr = int(body.decode())
            print(self.ptr)
            self.channel.stop_consuming()

        self.channel.basic_consume(queue='ptr', on_message_callback=callback, auto_ack=True)
        print("step-1")
        self.channel.start_consuming()
        return self.ptr