import mooncake_vllm_adaptor as mva

class MooncakeTransfer:
    def __init__(self):
        self.mva_ins = mva.mooncake_vllm_adaptor()
    
    def initialize(self, local_hostname: str, metadata_server: str, protocol: str, device_name: str):
        return self.mva_ins.initialize(local_hostname, metadata_server, protocol, device_name)

    def allocate_managed_buffer(self, length: int) -> int:
        return self.mva_ins.allocateManagedBuffer(length)
    
    def free_managed_buffer(self, buffer: int, length: int) -> int:
        return self.mva_ins.freeManagedBuffer(buffer, length)
    
    def transfer_sync(self, target_hostname: str, buffer: int, peer_buffer_address: int) -> int:
        return self.mva_ins.transferSync(target_hostname, buffer, peer_buffer_address)
    
if __name__ == "__main__":
    MooncakeTransfer mc = MooncakeTransfer()
    