# Minimal Packet Sender

Sends a single UDP packet from DPDK port 0 to designated destination.

Usage:
```
minimal_tx -- -m [dst MAC] -s [src IP] -d [dst IP]
```
For example:
```
./build/minimal_tx -- -m 0f:70:4a:e1:dd:34 -s 172.30.50.73 -d 172.30.50.194
```
As per usual, put DPDK EAL options ahead of `--` if needed.  For example:
```
./build/minimal_tx -v -- -m 0f:70:4a:e1:dd:34 -s 172.30.50.73 -d 172.30.50.194
```
