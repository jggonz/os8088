# PCI Ethernet driver

`PCIETHER.DRV` connects a PCI machine to a network, and through it to the
internet. It does everything the [[ETHER.DRV]] driver does, for the same
programs: the Browser, Telnet, the FTP Server, The Wire and WireChat. The
difference is the card. It drives a **Realtek RTL8139** network card in a
PCI slot, where the Ethernet driver drives an NE1000 or NE2000 in an ISA
slot.

## Which machines have it

It ships on the system disks of the standard build, the emulator build and
the protected-mode build for the 286 and later, except the 360K system disk:
no XT has a PCI slot. A PCI slot means a 386, a 486 or a Pentium, and the
driver looks for the card only on a 386 or better. On an older machine it
finds nothing and says so. Until you tick it, it is never loaded and costs
nothing.

It finds the card through the machine's PCI BIOS, so there is no address
or jumper to set. Any card that reports itself as an RTL8139, including the
8139A, B, C and C+, works.

## Turning it on

Open the **Control Panel**, choose **Drivers**, and tick **PCI Ethernet**.
The driver loads, finds the card and asks the network for an address. Close
the Control Panel to load it at every start.

The Ethernet driver and this one are two ways of reaching the same network,
and only one can be loaded. When both are ticked, the Ethernet driver is
tried first. If it finds its card, the PCI Ethernet box says **Class
already in use**; if it finds none, this driver loads. Untick the one you
are not using.

## The Ethernet page

Once the driver is loaded, the Control Panel's **Ethernet** page works as
the Ethernet driver's manual describes, with two differences:

| Line or button | On an RTL8139 |
|---|---|
| **Card** | **RTL8139**. There is no port address after it |
| **Buf** | **Buf 1K** or **Buf 4K**. Asking for 8K gives 4K, because the card's own receive buffer is 8K and a connection's buffer must fit inside it. The button shows the size in use |

**Renew**, **Set Up**, the **Ethernet Setup** window and setting the
address by hand are the same, and the address is kept in the same
`ETHER.CFG` file, so it carries over if you change drivers.

## Memory

The driver takes about 39K of memory while loaded: the driver itself, 11.5K
the card fills with incoming packets, and the buffers of the connections
programs open. The card's buffer always sits in the first 640K.

## Without a card

The box stays clear with **No hardware found** under it, and the Ethernet
page does not appear. The same words appear if the machine has a card but
not enough free memory for its buffer. Click the box again to stop the
machine trying at each start.

## Good to know

- In an emulator, give the machine an RTL8139 card: QEMU's `rtl8139`, or
  86Box's Realtek RTL8139C+.
- The driver checks the network for new packets whenever a program asks,
  as the Ethernet driver does. A program that is not using the network
  costs it nothing.
