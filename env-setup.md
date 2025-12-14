# F-Stack + DPDK Setup Guide (Ubuntu Server 24.04)

## Prerequisites

### Hardware
- You need a NIC that supports DPDK.  
- You can also test in a VirtualBox VM. **VirtualBox is only for learning and will not provide performance improvements.**

### System
- You need a clean environment with **root** access.
- The steps below were tested on **Ubuntu Server 24.04 in VirtualBox**.

### Network interruption warning (important)
- For a remote host, you should have **at least two NICs**:
  1. One NIC for SSH/management
  2. One NIC dedicated to F-Stack
- Once you start F-Stack, SSH on the NIC taken over by DPDK/F-Stack will drop, and you may lose control of the host if you only have one NIC.

---

## Step 1: Preparation and system setup (on a clean Ubuntu Server 24.04)

### Install dependencies
```bash
sudo apt update
sudo apt install -y build-essential cmake automake autoconf libtool \
     libnuma-dev libpcap-dev libevent-dev libpthread-stubs0-dev \
     libconfig-dev libssl-dev meson python3-pip python3-pyelftools \
     libsystemd-dev
```

### Clone F-Stack source code
```bash
cd ~
git clone https://github.com/F-Stack/f-stack.git
```

### Build DPDK (bundled with F-Stack)
```bash
cd ~/f-stack/dpdk
meson -Denable_kmods=true build
ninja -C build
sudo ninja -C build install
```

### Build F-Stack library
```bash
cd ~/f-stack/lib
make
sudo make install
```

### Build F-Stack examples
```bash
cd ~/f-stack/example
make
```

---

## Step 2: Check the current environment

### Check DPDK device status
```bash
cd ~/f-stack/dpdk
sudo ./usertools/dpdk-devbind.py --status
```

You should see output similar to the following, indicating there are two NICs and both are currently controlled by the kernel:

```
Network devices using kernel driver
===================================
0000:00:03.0 '82540EM Gigabit Ethernet Controller 100e' if=enp0s3 drv=e1000 unused= *Active*
0000:00:08.0 '82540EM Gigabit Ethernet Controller 100e' if=enp0s8 drv=e1000 unused=
```

### Check NIC IP addresses
```bash
ip a
```

Example output:

```
2: enp0s3: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    inet 10.0.2.15/24 metric 100 brd 10.0.2.255 scope global dynamic enp0s3
       valid_lft 81410sec preferred_lft 81410sec
3: enp0s8: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    inet 10.0.3.15/24 brd 10.0.3.255 scope global dynamic enp0s8
       valid_lft 86350sec preferred_lft 86350sec
```

In this example:
- `enp0s3` is used for SSH/remote control
- `enp0s8` is used for DPDK/F-Stack testing

### If the second NIC does not have an IP
You can obtain an IP via DHCP:

```bash
sudo apt install isc-dhcp-client
sudo ip link set enp0s8 up
sudo dhclient enp0s8
sudo ip link set enp0s8 down   # otherwise DPDK may fail to bind later
```

---

## Step 3: Start DPDK

### Configure hugepages
```bash
sudo sysctl -w vm.nr_hugepages=1024
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs none /mnt/huge
```

### Bind the NIC to a DPDK driver (choose ONE of the following)

#### Option A: VirtualBox VM
```bash
sudo modprobe uio_pci_generic
sudo ./usertools/dpdk-devbind.py -b uio_pci_generic 0000:00:08.0   # update the PCI address based on your --status output
```

#### Option B: Physical machine
```bash
sudo modprobe vfio-pci
sudo ./usertools/dpdk-devbind.py -b vfio-pci 0000:00:08.0          # update the PCI address based on your --status output
```

### Confirm status
```bash
sudo ./usertools/dpdk-devbind.py --status
```

Now the NIC should look like this, meaning one NIC is controlled by a DPDK-compatible driver:

```
Network devices using DPDK-compatible driver
============================================
0000:00:08.0 '82540EM Gigabit Ethernet Controller 100e' drv=uio_pci_generic unused=e1000,vfio-pci

Network devices using kernel driver
===================================
0000:00:03.0 '82540EM Gigabit Ethernet Controller 100e' if=enp0s3 drv=e1000 unused=vfio-pci,uio_pci_generic *Active*
```

---

## Step 4: Update the F-Stack configuration file

Edit:
```bash
vi ~/f-stack/config.ini
```

Mainly update the `port0` section using the IP information collected earlier:

```ini
[port0]
addr=10.0.3.15
netmask=255.255.255.0
broadcast=10.0.3.255
gateway=10.0.3.2
```

### How to confirm the gateway
Try pinging common gateway addresses and pick the one that responds:

```bash
ping -c 1 10.0.3.1
ping -c 1 10.0.3.254
ping -c 1 10.0.3.2
```

### VirtualBox port forwarding (for host access)
In the VirtualBox network settings, configure **port forwarding** for the second NIC so you can access the VM from the host.

Example: forward host port **8080** to VM port **80**.

---

## Step 5: Run an F-Stack example to validate the installation

### Start F-Stack
```bash
cd ~/f-stack
sudo ff_start
```

### Verify from the host
On the host machine, open:

- `http://127.0.0.1:8080/`

If you can see the expected page, then F-Stack is working properly.
