Intel® DPDK vSwitch supports the mapping of host-created Intel® DPDK objects directly into guest userspace, eliminating performance penalties presented by QEMU I/O emulation.

This section contains instructions on how to compile and run a sample application that demonstrates performance of Intel® DPDK vSwitch with IVSHM integration. It also describes the additional configuration required for both host and client systems to use IVSHM.

______

## Overview

This guide covers a Phy->VM->Phy configuration using IVSHM vports:

```
                                           __
                  +----------------------+   |
                  | guest                |   |
                  |                      |   |
                  |   +--------------+   |   |  guest
                  |   |  ovs_client  |   |   |
                  |   |              |   |   |
                  +---+--------------+---+ __|
                          ^      :
                          |      |
                          :      v                       __
    +-----------------+--------------+-----------------+   |
    |                 | ivshm vport1 |                 |   |
    |                 +--------------+                 |   |
    |                     ^      :                     |   |
    |          +----------+      +---------+           |   |  host
    |          :                           v           |   |
    |   +--------------+            +--------------+   |   |
    |   |   phy port   |  ovs-dpdk  |   phy port   |   |   |
    +---+--------------+------------+--------------+---+ __|
               ^                           :
               |                           |
               :                           v
    +--------------------------------------------------+
    |                                                  |
    |                traffic generator                 |
    |                                                  |
    +--------------------------------------------------+
```

______

## Initial Switch Setup

On the host, remove any configuration associated with a previous run of the switch:

```bash
pkill -9 ovs
rm -rf /usr/local/var/run/openvswitch/
rm -rf /usr/local/etc/openvswitch/
mkdir -p /usr/local/var/run/openvswitch/
mkdir -p /usr/local/etc/openvswitch/
rm -f /tmp/conf.db
```

Configure some environment variables:

```bash
cd openvswitch
export OPENVSWITCH_DIR=$(pwd)
```

Initialise the Open vSwitch database server:

```bash
./ovsdb/ovsdb-tool create /usr/local/etc/openvswitch/conf.db \
  $OPENVSWITCH_DIR/vswitchd/vswitch.ovsschema
./ovsdb/ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
  --remote=db:Open_vSwitch,Open_vSwitch,manager_options &
```

Add a bridge to the switch:

```bash
./utilities/ovs-vsctl --no-wait add-br br0 -- set Bridge br0 datapath_type=dpdk
```

Add ports to the bridge:

```bash
./utilities/ovs-vsctl --no-wait add-port br0 port1 -- set Interface port1 \
  type=dpdkphy ofport_request=1 option:port=0
./utilities/ovs-vsctl --no-wait add-port br0 port2 -- set Interface port2 \
  type=dpdkphy ofport_request=2 option:port=1
./utilities/ovs-vsctl --no-wait add-port br0 port3 -- set Interface port3 \
  type=dpdkclient ofport_request=3
```

Confirm the ports have been successfully added:

```bash
./utilities/ovs-vsctl show
```

You should see something like this:

```bash
00000000-0000-0000-0000-000000000000
    Bridge "br0"
        Port "br0"
            Interface "br0"
                type: internal
        Port "port1"
            Interface "port1"
                type: dpdkphy
                options: {port="0"}
        Port "port2"
            Interface "port2"
                type: dpdkphy
                options: {port="1"}
        Port "port3"
            Interface "port3"
                type: dpdkclient
```

Start `ovs-dpdk`:

```bash
./datapath/dpdk/build/ovs-dpdk -c 0x0F -n 4 --proc-type primary \
  --base-virtaddr=<virt_addr> -- --stats_core=0 --stats_int=5 -p 0x03
```

Start the Open vSwitch daemon:

```bash
./ovs-vswitchd -c 0x100 --proc-type=secondary -- --pidfile=/tmp/vswitchd.pid
```

______

## Flow Table Setup

Delete the default flow entries from the bridge:

```bash
./utilities/ovs-ofctl del-flows br0
```

Add flow entries to switch packets from `port1` (Phy 0) to `port3` (Client 1) on the ingress path, and from `port3` to `port2` (Phy 1) on the egress path:

```bash
./utilities/ovs-ofctl add-flow br0 in_port=1,dl_type=0x0800,nw_src=10.1.1.1,\
nw_dst=10.1.1.254,idle_timeout=0,action=output:3
./utilities/ovs-ofctl add-flow br0 in_port=3,dl_type=0x0800,nw_src=10.1.1.1,\
nw_dst=10.1.1.254,idle_timeout=0,action=output:2
```

______

## Guest Setup

### Copy Files

Copy files over to the VM making sure the `-L` flag is used within `cp` to follow the symlinks.

```bash
rm -rf /tmp/qemu_share
mkdir -p /tmp/qemu_share
chmod 777 /tmp/qemu_share
mkdir -p /tmp/qemu_share/DPDK
mkdir -p /tmp/qemu_share/ovs_client
cp -aL $RTE_SDK/* /tmp/qemu_share/DPDK
cp -aL $OPENVSWITCH_DIR/guest/ovs_client/* /tmp/qemu_share/ovs_client
```

### Create IVSHMEM metadata

Run the IVSHMEM manager utility to create the metadata needed to be used with QEMU. In this example `port3` is going to be shared over a metadata file named `vm_1`.

```bash
./utilities/ovs-ivshm-mngr -c 0x1 --proc-type=secondary -- vm_1:port3
```

Among other information the utility will print out to `STDOUT` the exact IVSHMEM command line to be used when launching QEMU. Add this to the other QEMU arguments.

```bash
APP: QEMU cmdline for metadata 'vm_1': -device ivshmem,size=4M,shm=fd:/dev/
  hugepages/rtemap_2:0x0:0x200000:/dev/zero:0x0:0x1fc000:/var/run/
  .dpdk_ivshmem_metadata_vm_1:0x0:0x4000
```

### Create VM

Start QEMU with the metadata created above, for example:

```bash
./qemu/x86_64-softmmu/qemu-system-x86_64 -cpu host -boot c                   \
   -hda <qemu_imagename.qcow2> -snapshot -m 8192M -smp 2                     \
  --enable-kvm -name 'client 1' -nographic -vnc :1 -pidfile /tmp/vm1.pid     \
  -drive file=fat:rw:/tmp/qemu_share,snapshot=off                            \
  -monitor unix:/tmp/vm1monitor,server,nowait                                \
  -device ivshmem,size=4M,shm=fd:/dev/hugepages/rtemap_2:0x0:0x200000:\
/dev/zero:0x0:0x1fc000:/var/run/.dpdk_ivshmem_metadata_vm_1:0x0:0x4000
```

______

## Guest Configuration

### Copy Files

Login to the VM and copy across the files.

```bash
mkdir -p /mnt/ovs
mount -o iocharset=utf8 /dev/sdb1 /mnt/ovs
mkdir -p /root/ovs
cp -a /mnt/ovs/* /root/ovs
```

### Build DPDK on the Guest

```bash
cd /root/ovs/DPDK
export RTE_SDK=$(pwd)
export RTE_TARGET=x86_64-ivshmem-linuxapp-gcc
export CC=gcc
make uninstall
make install T=x86_64-ivshmem-linuxapp-gcc
```

### Mount/Enable Hugepages on the Guest
```bash
mkdir -p /dev/hugepages
sudo mount -t hugetlbfs nodev /dev/hugepages
```
Assuming that hugepages haven't been enabled in the guest's kernel boot parameters:
```bash
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048/nr_hugepages
```

### Build `ovs_client` app

```bash
cd /root/ovs/ovs_client
make clean
make
```

### Client Commands

Having completed the above, run the `ovs_client` application.

```bash
./ovs_client -c 0x1 -n 4 -- -p port3
```

______

## Run the Test

Start transmission of packets matching the flows described in the Flow Table Setup section from the packet generator. If setup correctly, packets can be seen returning to the transmitting port from the DUT.

______

© 2014, Intel Corporation. All Rights Reserved
