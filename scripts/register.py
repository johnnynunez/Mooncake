import etcd3
import sys
import json
import os
import socket

if __name__ == "__main__":
  if len(sys.argv) < 2:
    print("Usage: python mount.py <file_path> ...")
    sys.exit(1)

  os.environ.pop("HTTP_PROXY", None)
  os.environ.pop("HTTPS_PROXY", None)
  os.environ.pop("http_proxy", None)
  os.environ.pop("https_proxy", None)

  files = sys.argv[1:]
  local_server_name = socket.gethostname()
  segment_name = "/mooncake/nvmeof/" + local_server_name
  print(segment_name)

  etcd = etcd3.client(host='localhost', port=2379)

  value = {}
  value['server_name'] = local_server_name
  value['protocol'] = "NVMeoF"
  value['buffers'] = []
  for file in files:
    buffer = {}
    buffer['length'] = os.path.getsize(file)
    buffer['file_path'] = file
    buffer['local_path_map'] = {}
    value['buffers'].append(buffer)
  
  print(json.dumps(value))
  etcd.put(segment_name, json.dumps(value))

