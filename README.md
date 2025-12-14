Usage

Kernel Server
```
gcc -O2 -Wall -o server_kernel server_kernel.c

./server_kernel
```

F-Stack Server
```
sudo sysctl -w vm.nr_hugepages=1024
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs none /mnt/huge

sudo modprobe vfio-pci
sudo ./usertools/dpdk-devbind.py -b vfio-pci 0000:00:08.0 // update last according to --status

gcc -O2 -Wall -I/f-stack/lib -I/f-stack/dpdk/build/include \
     -o server_fstack server_fstack.c \
     -L/f-stack/lib  -L/f-stack/dpdk/build/lib \
     -L/f-stack/dpdk/build/drivers -lfstack \
     -Wl,--as-needed \
     -lrte_eal -lrte_ethdev -lrte_mbuf -lrte_mempool -lrte_ring \
     -lrte_kvargs -lrte_net -lrte_log -lrte_timer -lrte_net_bond \
     -lcrypto -lpthread -ldl -lm

// modify config.ini [port0] if needed
sudo ./server_fstack
```

Client Side
```
gcc -O2 -Wall client.cpp -o client

// Usage: ./client <server_ip> <port> <msg_count> <payload_size|-1> [output_basename]
// payload -1 test all size from 64, 128, 256, ... 8192 
./client 192.168.5.220 8080 1000 -1 wsl-client-phy-kernel-srv

python3 create_graph.py win-client-phy-kernel-srv

// check output wsl-client-phy-kernel-srv.png
```

Please refer to env-setup.md for instructions on preparing F-Stack.
