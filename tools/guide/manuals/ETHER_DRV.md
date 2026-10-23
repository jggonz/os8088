# Ethernet driver

`ETHER.DRV` connects the machine to a network, and through it to the
internet. It drives an Ethernet card and carries the network protocols
programs need, so the Browser, Telnet, the FTP Server, The Wire and
WireChat can reach other computers.

![The Drivers page on a machine with no network card: Ethernet is unticked and says No hardware found.](shot:ether-drv-row)

## What is inside it

- The driver for a **Novell NE1000** or **NE2000** network card, or a card
  compatible with one. It works in an 8-bit slot, so an XT can use either.
- Everything that turns the card into a connection: getting an address from
  the network automatically, looking up names such as `os8088.com`, and the
  connections programs open.
- The Control Panel's **Ethernet** page and the **Ethernet Setup** window.

Secure `https://` pages are beyond what this machine can work out in
reasonable time, so the Browser refuses them by name. A helper program on a
modern computer can fetch them for it.

## Why it is on the system disk

Few machines of the period had a network card, and the driver is the
largest one: about 34K of memory while loaded. So it stays on the system
disk until you tick it.

## Turning it on

Open the **Control Panel**, choose **Drivers**, and tick **Ethernet**. The
driver loads, looks for the card and asks the network for an address. Close
the Control Panel to load it at every start.

The **Ports** button on the same page sets where the driver looks for the
card. **Auto** tries the usual addresses, 240h to 360h; pick one with the
arrows if two cards could be confused. The choice applies at the next start.
The Sound driver's page describes the Ports view with a picture.

## The Ethernet page

Once the driver is loaded, an **Ethernet** page appears in the Control
Panel's list. It names the card before anything else, so a missing address
cannot be mistaken for a missing card:

| Line | What it shows |
|---|---|
| **Card** | **Ne1000** or **Ne2000**, and where it was found |
| **Id** | The card's own hardware address |
| **Addr** | The machine's network address, or **none** |
| **Router** | The router everything outside the local network goes through |
| **Name** | The name server that looks up names |
| **Dhcp** | **Asking**, **Bound** once an address was handed out, **No server** if nothing answered, or **Manual** |
| **Rx** and **Tx** | How many packets came in and went out |

Three buttons sit under it:

- **Renew** asks the network for an address again. It is greyed when the
  address is set by hand.
- **Set Up** opens the Ethernet Setup window.
- **Buf** shows how much memory each connection has for data in transit:
  **Buf 1K**, **Buf 4K** or **Buf 8K**, or **Buf --** if there was not
  enough. Clicking it steps through automatic, 1K, 4K and 8K. Bigger is
  faster. It is greyed while a program has a connection open.

## Setting the address by hand

A network with no server to hand out addresses, or a cable straight to one
other PC, needs the address typed in. In **Ethernet Setup**, choose
**Manual** and fill in **Address** and **Subnet**; **Router** and **Name**
may stay empty when there is none. **Ok** uses the new address at once, and
a mistyped field is named on the window's message line, such as
**Address?**. **Automatic** goes back to asking the network.

The address and the Buf choice are kept in a small file, `ETHER.CFG`, on
the disk the machine started from, written when you close the Control
Panel.

## Without a network card

The box stays clear with **No hardware found** under it, and the Ethernet
page does not appear. Programs that need the network say so when they try
to connect. Click the box again to stop the machine trying at each start.

## Good to know

- In an emulator, give the machine an NE2000 card; the driver needs one to
  load.
- The parallel link (os88net) gives Telnet and the Browser a second way out,
  through a DOS PC on a cable.
- The small system for 128KB machines loads no drivers and leaves it off.
