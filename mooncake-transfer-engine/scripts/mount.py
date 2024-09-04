import etcd3
import sys
import json
import os
import socket
import subprocess

def discover_nvmeof_targets(nqn, transport, traddr, trsvcid):
    """Discover NVMe-oF targets."""
    cmd = f"nvme discover -t {transport} -a {traddr} -s {trsvcid}"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Discovery failed: {result.stderr}")
        return None
    print(f"Discovered NVMe-oF targets:\n{result.stdout}")
    return result.stdout

def connect_nvmeof_target(nqn, transport, traddr, trsvcid):
    """Connect to NVMe-oF target."""
    cmd = f"nvme connect -t {transport} -n {nqn} -a {traddr} -s {trsvcid}"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Connection failed: {result.stderr}")
        return None
    print(f"Connected to NVMe-oF target:\n{result.stdout}")
    return result.stdout

def mount_nvme_device(device, mount_point):
    """Mount the NVMe device to a specific mount point."""
    os.makedirs(mount_point, exist_ok=True)
    cmd = f"mount {device} {mount_point}"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Mount failed: {result.stderr}")
        return None
    print(f"Mounted {device} on {mount_point}")
    return mount_point

if __name__ == "__main__":
  if len(sys.argv) != 4:
    print("Usage: python mount.py <segment_name> <file_path> <local_path>")
    sys.exit(1)

  os.environ.pop("HTTP_PROXY", None)
  os.environ.pop("HTTPS_PROXY", None)
  os.environ.pop("http_proxy", None)
  os.environ.pop("https_proxy", None)
  segment_name = sys.argv[1]
  file_path = sys.argv[2]
  local_path = sys.argv[3]

  local_server_name = "optane14"

  etcd = etcd3.client(host='optane12', port=2379)
  value, _ = etcd.get(segment_name)

  if value is None:
    print("Segment not found")
    sys.exit(1)

  segment = json.loads(value)
  buffers = segment["buffers"]
  print(buffers)

  buffer = next((b for b in buffers if b["file_path"] == file_path), None)
  buffer['local_path_map'][local_server_name] = local_path

  etcd.put(segment_name, json.dumps(segment))
  print(etcd.get(segment_name)[0])

  # TODO: mount the buffer to local_path


