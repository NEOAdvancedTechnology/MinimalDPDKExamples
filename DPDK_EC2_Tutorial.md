# DPDK on AWS EC2 Tutorial

*DPDK is a very large and complex toolkit.  Here is a step-by-step mechanism to get it running on AWS EC2.*

1. For this tutorial, launch an EC2 instance with RHEL AMI, such as Red Hat Enterprise Linux 7.6.  (Other AMIs can work with DPDK, but we'll use RHEL for this tutorial).
2. Choose an instance type that supports the Elastic Network Adapter (ENA).  A list is here: <https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/enhanced-networking-ena.html>.  For example, c5.9xlarge. If you launch with the right instance type and AMI you don't have to do anything special to enable the ENA.
3. We will use `eth0` for ssh control of the EC2 instance.  Add at least one additional Network Interface for DPDK.
4. Configure an appropriate Security Group to let in the traffic you are interested in.
5. Because your EC2 instance has more than one Network Interface, you'll have to set up an Elastic IP to access the EC2 instance from outside AWS:  
	* Go to the EC2 console "Description" tab and look at "Network interfaces" section.
	* Click on "eth0", and make note of "Interface ID"
	* Go to "Elastic IPs" and "Allocate new address" with default options.
	* As an action on the Elastic IP, "Associate address" with the "Network interface" that has the interface ID of eth0 noted earlier.
6. Now you can ssh into your instance on its new public IP.
7. Time to get some essentials:
```
sudo -i yum install -y git gcc openssl-devel kernel-devel-$(uname -r) bc numactl-devel make net-tools vim pciutils iproute wget
```
8. Download DPDK from [https://core.dpdk.org/download/](https://core.dpdk.org/download/).  I suggest 18.11.0 (LTS). For example:
```
wget https://fast.dpdk.org/rel/dpdk-18.11.tar.xz
tar xf dpdk*.tar.xz
cd dpdk*
```		
9. Assuming you are on x86_64 linux with gcc (otherwise see DPDK documentation):
```
sudo su
export RTE_SDK=`pwd`
make config install T=x86_64-native-linuxapp-gcc DESTDIR=$RTE_SDK
```		
10. Reserve hugepages:
```
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

Alternatively, if you want to set reservation of hugepages permanently so you don't have to type this every time you reboot, you can append the reservation to sysctl.conf in RHEL:
```
echo "vm.nr_hugepages=1024" >> /etc/sysctl.conf
```

11. In the RHEL AMI, it is likely a file system of
type `hugetlbfs` has already been mounted.  You can check this:
```
# cat /proc/mounts | grep hugetlbfs
hugetlbfs /dev/hugepages hugetlbfs rw,seclabel,relatime 0 0
```
If it is not there for some reason, you should mount one:
```
mkdir /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
```  
12. You can check on the huge pages:
```
# cat /proc/meminfo | grep HugePages_
HugePages_Total:    1024
HugePages_Free:     1024
HugePages_Rsvd:        0
HugePages_Surp:        0
```	
13. Disable Address-Space Layout Randomization (ASLR)
```
echo 0 > /proc/sys/kernel/randomize_va_space
```	
Alternatively, if you would like to make this permanent and not have to type it every reboot, you can append it to sysctl.conf:
```
echo "kernel.randomize_va_space=0" >> /etc/sysctl.conf
```

14.  Install kernel modules:
```
modprobe uio
modprobe hwmon
insmod x86_64-native-linuxapp-gcc/kmod/igb_uio.ko
insmod x86_64-native-linuxapp-gcc/kmod/rte_kni.ko  	
```
15. Take down interface(s) you will use with DPDK:
```
ifconfig eth1 down
```		
16. Now look at the NIC drivers which are bound:
```
# ./usertools/dpdk-devbind.py --status

Network devices using kernel driver
===================================
0000:00:05.0 'Elastic Network Adapter (ENA) ec20' if=eth0 drv=ena unused=igb_uio *Active*
0000:00:06.0 'Elastic Network Adapter (ENA) ec20' if=eth1 drv=ena unused=igb_uio 
...
```		
17. Bind the DPDK NIC(s) to the `igb_uio` driver using the last part of the PCI "bus:slot.func" address you saw from `dpdk-devbind.py --status`.  Then confirm
```
# ./usertools/dpdk-devbind.py --bind=igb_uio 00:06.0
# ./usertools/dpdk-devbind.py --status

Network devices using DPDK-compatible driver
============================================
0000:00:06.0 'Elastic Network Adapter (ENA) ec20' drv=igb_uio unused=ena

Network devices using kernel driver
===================================
0000:00:05.0 'Elastic Network Adapter (ENA) ec20' if=eth0 drv=ena unused=igb_uio *Active* 
...
```
18. Make and run an example:
```shell
cd examples/helloworld
make
# ./build/helloworld 
EAL: Detected 36 lcore(s)
EAL: Detected 1 NUMA nodes
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket
EAL: No free hugepages reported in hugepages-1048576kB
EAL: Probing VFIO support...
EAL: PCI device 0000:00:05.0 on NUMA socket -1
EAL:   Invalid NUMA socket, default to 0
EAL:   probe driver: 1d0f:ec20 net_ena
EAL: PCI device 0000:00:06.0 on NUMA socket -1
EAL:   Invalid NUMA socket, default to 0
EAL:   probe driver: 1d0f:ec20 net_ena
EAL: PCI device 0000:00:07.0 on NUMA socket -1
EAL:   Invalid NUMA socket, default to 0
EAL:   probe driver: 1d0f:ec20 net_ena
hello from core 1
hello from core 2
hello from core 3
...
hello from core 34
hello from core 35
hello from core 0
```

### Notes:
EC2 networking will still route IP packets to the private IP addresses of the DPDK-enabled Network Interfaces as identified in the AWS Console.

You can add additional Network Interfaces onto your EC2 instance beyond `eth1` by going to the "Network Interfaces" tab on the AWS Console and clicking on "Create Network Interface", then "attaching" them to your instance.

For DPDK applications, you may need to know the MAC and IP addresses of the a network interface attached with DPDK.  An easy way to do this from the AWS Console under EC2 Instances is to click on the Network Interface of interest (such as `eth1`) where the Private IP Address will be shown, and then click on the Interface ID which will show you the MAC address. 
