Intel® DPDK vSwitch provides a number of reference applications for guests. These can be found in the `guest` subdirectory of the Intel® DPDK vSwitch package. Each application is detailed in this section.

______

## ovs_client

`ovs_client` is a simple reference application which demonstrates the performance benefits of IVSHM-based I/O.  Each instance of the application running within a guest connects to a `CLIENT` port on the switch, and simply returns any traffic it receives from that port to the switch.

The `ovs_client` application can be executed as follows:

```bash
./ovs_client [eal] -- -p <client port name>
```

### Args

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg].

After the EAL arguments, the following application-specific arguments are supported:

* `-p <client port name>`
  Name of a `CLIENT` port to bind to.

### Example Command

To "bind" to an IVSHM port called `clientport0`:

```bash
./ovs_client –c 0xf -n 4 -- -p clientport0
```

______

© 2014, Intel Corporation. All Rights Reserved

[dpdkorg-dpdkgsg]: http://dpdk.org/doc
