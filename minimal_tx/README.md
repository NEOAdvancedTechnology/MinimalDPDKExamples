# Minimal Packet Sender

Usage: `minimal_tx -- -m [dst MAC] -s [src IP] -d [dst IP]`.  As per usual, put DPDK RTE commands ahead of `--` if needed.  For example:
``'
./build/minimal_tx -- -m 0f:70:4a:e1:dd:34 -s 172.30.50.73 -d 172.30.50.194
```
