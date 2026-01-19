gcc -O2 -Wall -I/home/alan/_src/f-stack/lib -I/home/alan/_src/f-stack/dpdk/build/include \
     -o server_fstack server_fstack.cpp \
     -L/home/alan/_src/f-stack/lib  -L/home/alan/_src/f-stack/dpdk/build/lib \
     -L/home/alan/_src/f-stack/dpdk/build/drivers -lfstack \
     -Wl,--as-needed \
     -lrte_eal -lrte_ethdev -lrte_mbuf -lrte_mempool -lrte_ring \
     -lrte_kvargs -lrte_net -lrte_log -lrte_timer -lrte_net_bond \
     -lcrypto -lpthread -ldl -lm
