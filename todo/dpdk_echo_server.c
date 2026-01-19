// dpdk_echo_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define PORT_ID 0
#define BURST_SIZE 32
#define MEMPOOL_CACHE_SIZE 256
#define NUM_MBUFS 8191

static volatile bool force_quit = false;

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

int main(int argc, char **argv) {
    int ret;
    struct rte_mempool *mbuf_pool;
    uint16_t nb_ports;
    
    // 初始化 EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
    
    argc -= ret;
    argv += ret;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建内存池
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    
    // 检查端口数
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");
    
    printf("Number of available ports: %u\n", nb_ports);
    
    // 简单的端口配置（这里简化，实际需要更多配置）
    struct rte_eth_conf port_conf = {
        .rxmode = {
            .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
        },
    };
    
    // 配置端口 0
    ret = rte_eth_dev_configure(PORT_ID, 1, 1, &port_conf);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n", ret, PORT_ID);
    
    // 设置 RX/TX 队列
    ret = rte_eth_rx_queue_setup(PORT_ID, 0, 128, rte_eth_dev_socket_id(PORT_ID), NULL, mbuf_pool);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n", ret, PORT_ID);
    
    ret = rte_eth_tx_queue_setup(PORT_ID, 0, 512, rte_eth_dev_socket_id(PORT_ID), NULL);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n", ret, PORT_ID);
    
    // 启动设备
    ret = rte_eth_dev_start(PORT_ID);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n", ret, PORT_ID);
    
    printf("DPDK Echo Server started on port %u\n", PORT_ID);
    printf("Press Ctrl+C to stop\n");
    
    // 主循环：接收并回显数据包
    while (!force_quit) {
        struct rte_mbuf *bufs[BURST_SIZE];
        uint16_t nb_rx = rte_eth_rx_burst(PORT_ID, 0, bufs, BURST_SIZE);
        
        if (nb_rx > 0) {
            // 简单回显：接收到什么就发送回去
            uint16_t nb_tx = rte_eth_tx_burst(PORT_ID, 0, bufs, nb_rx);
            
            // 释放未发送的包
            if (unlikely(nb_tx < nb_rx)) {
                uint16_t buf;
                for (buf = nb_tx; buf < nb_rx; buf++)
                    rte_pktmbuf_free(bufs[buf]);
            }
        }
    }
    
    printf("\nStopping port %u...\n", PORT_ID);
    rte_eth_dev_stop(PORT_ID);
    rte_eth_dev_close(PORT_ID);
    
    printf("Bye!\n");
    return 0;
}
