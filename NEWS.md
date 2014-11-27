# v1.2 - November 2014

- Updated to use Intel(R) DPDK 1.7.1
- Datapath:
  - Added DPI support
  - Added Jumbo Frame Support
  - Re-added vEth support. This effectively re-adds OFTest support
  - Re-enabled the jobs system
  - Bug fixes
- OVS:
  - Convert 'openvswitch' folder to a subtree
  - Upgrade from 2.0.0 to 2.1.2
- Logs:
  - Increased debug and error logging
  - Added command line support for adjusting log levels
  - Added logging of NIC level stats

# v1.1 - August 2014

- Updated to use version of Intel(R) DPDK v1.7.0 available from [DPDK.org](http://www.dpdk.org/)
- Datapath:
  - Added multicore support - datapath may run on multiple pipelines/cores, with added load-balancing (round-robin)
  - Added support for `stats_core` argument to pin stats display to a specific core
  - Added support for `-p` portmask argument for physical devices
- QEMU:
  - Convert `qemu` folder to a subtree
  - Upgraded from v1.4.0 to v1.6.2, removing QEMU support for legacy VirtIO in the process
- Bug fixes

# v1.0 - May 2014

- Updated to use Intel(R) DPDK 1.7.0
- Datapath:
  - Ported to Intel(R) DPDK Packet Framework
  - Added support for Phy ports
  - Added support for IVSHM ports
  - Added support for Userspace vHost ports
- Daemon:
  - Added integration with packet framework implementation of datapath
- Bug fixes

# v0.10 - March 2014

- Added support for *Port Management* - the real management of devices in the
  datapath via the dpif (and utilities such as `ovs-vsctl`)
- Added support for arbitrary port names, i.e. port names with names other
  than `ovs_dpdk_xx`.
- Added top level Makefile
- Added new IVSHMEM utility application to manage port sharing with guests
  - Added more flexibility to what is being shared with each guest through command line
  - Separated DPDK IVSHMEM code from Open vSwitch datapath
- Added new public API to cleanly open up internal `vport.c` features with external clients
  - Added support to both `ovs_client` and `kni_client` applications
- Added new userspace vhost device type based on the DPDK sample application.
- Bug fixes

# v0.9 - January 2014

- Upgraded the base version of Open vSwitch from 1.5 to 2.0
- Updated to use Intel(R) DPDK 1.6.0
- Added Intel(R) DPDK 1.6.0 IVSHMEM support.
  - Added different hugepage sizes support - 2MB and 1GB hugepages
  - Added Intel(R) Atom processors support
  - Improved security and isolation between Host and Guest applications
  - Added support for Guest applications running as DPDK primary processes
  - NOTE: this update removes compatibility with DPDK 1.5.2
- Performance Improvements
- Support for set actions:
  - Added set ethernet support
  - Added set IPv4 support
  - Added set UDP support
  - Added set TCP support
- Bug fixes

# v0.8 - December 2013

- Added partial support for `ovs-testsuite` - a collection of unit tests found in standard Open vSwitch
- Added support for Host KNI, a.k.a. KNI vEth. This, in turn, added support for [OFTest](http://www.projectfloodlight.org/oftest/)
- Updated to use Intel(R) DPDK 1.5.2
  - NOTE: this update removes KNI compatibility with DPDK 1.4
- Bug fixes

# v0.7 - November 2013

- Datapath:
  - Added IMCP packet support
  - Added `strip_vlan` action support
  - Added `mod_vlan_vid` action support
  - Added `mod_vlan_pcp` action support
  - Added `drop` action support
  - Added support for multiple actions per flow
- Bug fixes

# v0.6 - October 2013

- Datapath:
  - Added 802.1Q VLAN-tagged frame support
- Performance Improvements
- Bug fixes

# v0.5 - August 2013

- Datapath:
  - Added support for dynamic flow allocation using the `ovs-dpctl` and `ovs-ofctl` applications
- Bug fixes

# v0.4 - July 2013

- Add support for numerous IO virtualization mechanisms: Virtio, IVSHM, and IVSHM with Intel(R) DPDK KNI
