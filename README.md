# MinimalDPDKExamples
Minimal examples of DPDK

## minimal_tx
Minimal code to send one UDP packet with DPDK

Sends one UDP packet, IP address and MAC address of target need to be set in program

## minimal_rx
Minimal code to receive packets with DPDK

Loops and prints hex dump of received packets on all DPDK interfaces

## Building
Requires DPDK installation and RTE_SDK environment variable set properly for Makefiles.

## Testing
These programs have been tested on AWS EC2 c5.9xlarge with RHEL AMI and added ENA interfaces.  See [DPDK on EC2 Tutorial](https://github.com/FOXNEOAdvancedTechnology/MinimalDPDKExamples/blob/master/DPDK_EC2_Tutorial.md) to rapidly install DPDK on AWS EC2, and to add ENA interfaces with DPDK drivers.
