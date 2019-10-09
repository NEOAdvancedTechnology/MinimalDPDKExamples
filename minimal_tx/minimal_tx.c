// Minimal DPDK TX
// Just send one packet from one port
//
// Thomas Edwards, Walt Disney Television
//
// Contains code from:
// https://github.com/DPDK/dpdk/blob/master/examples/helloworld/main.c
// https://github.com/DPDK/dpdk/blob/master/examples/skeleton/basicfwd.c
// Those files contain the following information:
/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_ethdev.h>
#include <rte_udp.h>
#include <rte_ip.h>

#define RX_RING_SIZE 0 
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

#define UDP_SRC_PORT 6666
#define UDP_DST_PORT 6666

#define IP_DEFTTL  64   /* from RFC 1340. */
#define IP_VERSION 0x40
#define IP_HDRLEN  0x05 /* default IP header length == five 32-bits words. */
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)

#define TX_PACKET_LENGTH 862

#if RTE_BYTE_ORDER == RTE_BIG_ENDIAN
#define RTE_BE_TO_CPU_16(be_16_v)  (be_16_v)
#define RTE_CPU_TO_BE_16(cpu_16_v) (cpu_16_v)
#else
#define RTE_BE_TO_CPU_16(be_16_v) \
        (uint16_t) ((((be_16_v) & 0xFF) << 8) | ((be_16_v) >> 8))
#define RTE_CPU_TO_BE_16(cpu_16_v) \
        (uint16_t) ((((cpu_16_v) & 0xFF) << 8) | ((cpu_16_v) >> 8))
#endif

uint64_t DST_MAC;
uint32_t IP_SRC_ADDR,IP_DST_ADDR;

static const struct rte_eth_conf port_conf_default = {
        .rxmode = { .max_rx_pkt_len = RTE_ETHER_MAX_LEN }
};

static struct rte_ipv4_hdr  pkt_ip_hdr;  /**< IP header of transmitted packets. */
static struct rte_udp_hdr pkt_udp_hdr; /**< UDP header of transmitted packets. */
struct rte_ether_addr my_addr; // SRC MAC address of NIC

struct rte_mempool *mbuf_pool;

uint32_t string_to_ip(char *);
uint64_t string_to_mac(char *);
static void send_packet(void);
static void exit_stats(int);

static uint64_t packet_count = 0;
static time_t t1;

// convert a quad-dot IP string to uint32_t IP address
uint32_t string_to_ip(char *s) {
	unsigned char a[4];
	int rc = sscanf(s, "%hhd.%hhd.%hhd.%hhd",a+0,a+1,a+2,a+3);
	if(rc != 4){
        	fprintf(stderr, "bad source IP address format. Use like: -s 198.19.111.179\n");
        	exit(1);
	}
	
	return
		(uint32_t)(a[0]) << 24 |
		(uint32_t)(a[1]) << 16 |
		(uint32_t)(a[2]) << 8 |
		(uint32_t)(a[3]);

}

// convert six colon separated hex bytes string to uint64_t Ethernet MAC address
uint64_t string_to_mac(char *s) {
    unsigned char a[6];
    int rc = sscanf(s, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                    a + 0, a + 1, a + 2, a + 3, a + 4, a + 5);
    if(rc !=6 ){
	fprintf(stderr, "bad MAC address format. Use like: -m 0a:38:ca:f6:f3:20\n");
	exit(1);
    }
	
    return
        (uint64_t)(a[0]) << 40 |
        (uint64_t)(a[1]) << 32 |
        (uint64_t)(a[2]) << 24 |
        (uint64_t)(a[3]) << 16 |
        (uint64_t)(a[4]) << 8 |
        (uint64_t)(a[5]);
}

// setup packet UDP & IP headers
static void setup_pkt_udp_ip_headers(struct rte_ipv4_hdr *ip_hdr,
                         struct rte_udp_hdr *udp_hdr,
                         uint16_t pkt_data_len)
{
        uint16_t *ptr16;
        uint32_t ip_cksum;
        uint16_t pkt_len;

	//Initialize UDP header.
        pkt_len = (uint16_t) (pkt_data_len + sizeof(struct rte_udp_hdr));
        udp_hdr->src_port = rte_cpu_to_be_16(UDP_SRC_PORT);
        udp_hdr->dst_port = rte_cpu_to_be_16(UDP_DST_PORT);
        udp_hdr->dgram_len      = RTE_CPU_TO_BE_16(pkt_len);
        udp_hdr->dgram_cksum    = 0; /* No UDP checksum. */

	//Initialize IP header.
        pkt_len = (uint16_t) (pkt_len + sizeof(struct rte_ipv4_hdr));
        ip_hdr->version_ihl   = IP_VHL_DEF;
        ip_hdr->type_of_service   = 0;
        ip_hdr->fragment_offset = 0;
        ip_hdr->time_to_live   = IP_DEFTTL;
        ip_hdr->next_proto_id = IPPROTO_UDP;
        ip_hdr->packet_id = 0;
        ip_hdr->total_length   = RTE_CPU_TO_BE_16(pkt_len);
        ip_hdr->src_addr = rte_cpu_to_be_32(IP_SRC_ADDR);
        ip_hdr->dst_addr = rte_cpu_to_be_32(IP_DST_ADDR);

 	//Compute IP header checksum.
        ptr16 = (unaligned_uint16_t*) ip_hdr;
        ip_cksum = 0;
        ip_cksum += ptr16[0]; ip_cksum += ptr16[1];
        ip_cksum += ptr16[2]; ip_cksum += ptr16[3];
        ip_cksum += ptr16[4];
        ip_cksum += ptr16[6]; ip_cksum += ptr16[7];
        ip_cksum += ptr16[8]; ip_cksum += ptr16[9];

	//Reduce 32 bit checksum to 16 bits and complement it.
        ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) +
                (ip_cksum & 0x0000FFFF);
        if (ip_cksum > 65535)
                ip_cksum -= 65535;
        ip_cksum = (~ip_cksum) & 0x0000FFFF;
        if (ip_cksum == 0)
                ip_cksum = 0xFFFF;
        ip_hdr->hdr_checksum = (uint16_t) ip_cksum;
}

// actually send the packet
static void send_packet(void)
{
	struct rte_mbuf *pkt;
        union {
                uint64_t as_int;
                struct rte_ether_addr as_addr;
        } dst_eth_addr;
	struct rte_ether_hdr eth_hdr;
	struct rte_mbuf *pkts_burst[1];

	pkt = rte_mbuf_raw_alloc(mbuf_pool);
        if(pkt == NULL) {printf("trouble at rte_mbuf_raw_alloc\n"); return;}
        rte_pktmbuf_reset_headroom(pkt);
        pkt->data_len = TX_PACKET_LENGTH;
	
        // set up addresses 
        dst_eth_addr.as_int=rte_cpu_to_be_64(DST_MAC);
        rte_ether_addr_copy(&dst_eth_addr.as_addr,&eth_hdr.d_addr);
        rte_ether_addr_copy(&my_addr, &eth_hdr.s_addr);
        eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	// copy header to packet in mbuf
	rte_memcpy(rte_pktmbuf_mtod_offset(pkt,char *,0),
		&eth_hdr,(size_t)sizeof(eth_hdr));
	rte_memcpy(rte_pktmbuf_mtod_offset(pkt,char *,sizeof(struct rte_ether_hdr)),
		&pkt_ip_hdr,(size_t)sizeof(pkt_ip_hdr));
	rte_memcpy(rte_pktmbuf_mtod_offset(pkt,char *,
		sizeof(struct rte_ether_hdr)+sizeof(struct rte_ipv4_hdr)),
		&pkt_udp_hdr,(size_t)sizeof(pkt_udp_hdr));

	// Add some pkt fields
        pkt->nb_segs = 1;
        pkt->pkt_len = pkt->data_len;
        pkt->ol_flags = 0;

	// Actually send the packet
	pkts_burst[0] = pkt;
        const uint16_t nb_tx = rte_eth_tx_burst(0, 0, pkts_burst, 1);
	packet_count += nb_tx;
	rte_mbuf_raw_free(pkt);
}

// Initialize Port
static inline int
port_init(uint16_t port)
{
        struct rte_eth_conf port_conf = port_conf_default;
        const uint16_t rx_rings = 0, tx_rings = 1;
 	uint16_t nb_rxd = RX_RING_SIZE;
        uint16_t nb_txd = TX_RING_SIZE;
        int retval;
        uint16_t q;
        struct rte_eth_dev_info dev_info;
        struct rte_eth_txconf txconf;

        if (!rte_eth_dev_is_valid_port(port))
                return -1;

        rte_eth_dev_info_get(port, &dev_info);
        if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
                port_conf.txmode.offloads |=
                        DEV_TX_OFFLOAD_MBUF_FAST_FREE;

        /* Configure the Ethernet device. */
        retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
        if (retval != 0)
                return retval;

        retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
        if (retval != 0)
                return retval;

        txconf = dev_info.default_txconf;
        txconf.offloads = port_conf.txmode.offloads;

        //Allocate and set up 1 TX queue
        for (q = 0; q < tx_rings; q++) {
                retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                rte_eth_dev_socket_id(port), &txconf);
                if (retval < 0)
                        return retval;
        }

        /* Start the Ethernet port. */
        retval = rte_eth_dev_start(port);
        if (retval < 0)
                return retval;

        /* get the port MAC address. */
        rte_eth_macaddr_get(port, &my_addr);
        printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
                        port,
                        my_addr.addr_bytes[0], my_addr.addr_bytes[1],
                        my_addr.addr_bytes[2], my_addr.addr_bytes[3],
                        my_addr.addr_bytes[4], my_addr.addr_bytes[5]);

        return 0;
}

static void exit_stats(int sig)
{
	time_t total_time;
	total_time = time(NULL) - t1;
	printf("Caught signal %d\n", sig);
	printf("\n=============== Stats =================\n");
	printf("Total packets: %lu\n", packet_count);
	printf("Total transmission time: %ld seconds\n", total_time);
	printf("Average transmission rate: %lu pps\n", packet_count / total_time);
	printf("                           %lu Gpbs\n", ((packet_count * TX_PACKET_LENGTH * 8) / total_time) / 1000000000);
	printf("=======================================\n");
	exit(0);
}

int main(int argc, char **argv)
{
	int ret,c;
	uint16_t pkt_data_len;
	int mac_flag=0,ip_src_flag=0,ip_dst_flag=0;
	
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_panic("Cannot init EAL\n");

	argc -= ret;
	argv += ret;

	signal (SIGINT, exit_stats);

	while ((c = getopt(argc, argv, "m:s:d:h")) != -1)
		switch(c) {
		case 'm':
			// note, not quite sure why last two bytes are zero, but that is how DPDK likes it
			DST_MAC=0ULL;
			DST_MAC=string_to_mac(optarg)<<16;	
			mac_flag=1;	
			break;
		case 's':
			IP_SRC_ADDR=string_to_ip(optarg);
			ip_src_flag=1;
			break;
                case 'd':
                        IP_DST_ADDR=string_to_ip(optarg);
                        ip_dst_flag=1;
                        break;
		case 'h':
			printf("usage -- -m [dst MAC] -s [src IP] -d [dst IP]\n");
			exit(0);
			break;
		}

	if(mac_flag==0) {
		fprintf(stderr, "missing -m for destination MAC adress\n");
		exit(1);
	}
	if(ip_src_flag==0) {
                fprintf(stderr, "missing -s for IP source adress\n");
                exit(1);
        }
        if(ip_dst_flag==0) {
                fprintf(stderr, "missing -d for IP destination adress\n");
                exit(1);
        }

        /* Creates a new mempool in memory to hold the mbufs. */
        mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
                MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

        if (mbuf_pool == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	// initialize port 0
	if (port_init(0) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port 0\n");


	printf("Sending packets ... [Press Ctrl+C to exit]\n");

        pkt_data_len = (uint16_t) (TX_PACKET_LENGTH - (sizeof(struct rte_ether_hdr) +
                                                    sizeof(struct rte_ipv4_hdr) +
                                                    sizeof(struct rte_udp_hdr)));
        setup_pkt_udp_ip_headers(&pkt_ip_hdr, &pkt_udp_hdr, pkt_data_len);

	t1 = time(NULL);
	while (true)
		send_packet();

	return(0);
}
