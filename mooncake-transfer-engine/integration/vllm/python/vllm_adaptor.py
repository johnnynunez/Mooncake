import mooncake_vllm_adaptor as mva

class MooncakeTransfer:
    """
    A class help users to use mooncaketransfer in Python
    ...
    Attributes
    ----------
    mva_ins : mooncake_vllm_adaptor
        mooncake vllm adaptor
    """
    def __init__(self):
        self.mva_ins = mva.mooncake_vllm_adaptor()
    
    def initialize(self, local_hostname: str, metadata_server: str, protocol: str, device_name: str):
        """ Init Mooncake Instance, both sides should do this.
        Parameters
        ----------
        local_hostname : str
            [locahost IP:port], 
        metadata_server : str
            "192.168.0.139:2379" (fixed)
        protocol : str
            "rdma" (fixed)
        device_name : str
            "erdma_0" (fixed) 
        Returns
        -------
        list
            a list of strings used that are the header columns
        """
        return self.mva_ins.initialize(local_hostname, metadata_server, protocol, device_name)

    def allocate_managed_buffer(self, length: int) -> int:
        ret = self.mva_ins.allocateManagedBuffer(length)
        if ret <= 0:
            raise Exception("Allocation Return Error")
        return ret
    
    def free_managed_buffer(self, buffer: int, length: int) -> int:
        return self.mva_ins.freeManagedBuffer(buffer, length)
    
    def transfer_sync(self, target_hostname: str, buffer: int, peer_buffer_address: int, length: int) -> int:
        ret = self.mva_ins.transferSync(target_hostname, buffer, peer_buffer_address, length)
        if ret < 0:
            raise Exception("Transfer Return Error")
        return ret
        
    def write_bytes_to_buffer(self, buffer: int, user_data: str, length: int) -> int:
        return self.mva_ins.writeBytesToBuffer(buffer, user_data, length)

    def read_bytes_from_buffer(self, buffer: int, length: int) -> str:
        return self.mva_ins.readBytesFromBuffer(buffer, length)
