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

    def recv_ptr(self):
        def callback(ch, method, properties, body):
            print("step-2")
            self.ptr = int(body.decode())
        self.channel.basic_consume(queue='ptr', on_message_callback=callback, auto_ack=True)
        print("step-1")

        self.channel.start_consuming()
        print(self.ptr)
        return self.ptr