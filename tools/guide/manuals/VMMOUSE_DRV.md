# Emulator pointer

`VMMOUSE.DRV` makes the pointer follow your real mouse exactly when os8088
runs in an emulator or a virtual machine. Without it, the emulator has to
capture your mouse before the pointer moves, and you press a key to get it
back. With it, the pointer simply sits wherever your mouse is over the
emulator's window, with nothing to capture or release.

## What is inside it

It speaks the "VMware mouse" protocol, which many emulators offer: v86 (the
emulator behind os8088's web demo), QEMU, VMware and VirtualBox. The
emulator tells the driver where the mouse is, and the driver puts the
pointer there and passes on the buttons.

The protocol needs a 386 or later, so the driver only works on an emulated
386 or better.

## Why it is only on this disk

It is on the **emulator system disk** and on no other. On a real PC or an
emulated XT there is nothing for it to talk to, so the ordinary system disks
leave it off, and the system on them carries none of the code that goes
with it. The emulator system disk has its own copy of os8088 that does.

On that disk the driver is already switched on, so the pointer works from
the first moment with no setting to change.

## Using it

There is nothing to do on the emulator system disk. Its row on the Control
Panel's **Drivers** page is called **Absolute mouse**. To go back to the
emulator's own captured mouse, untick it, close the Control Panel to keep
the choice, and restart.

When it is working, it is the machine's only mouse: a serial mouse on the
same machine is not read.

## Without the hardware

On a machine below a 386, or an emulator without the VMware mouse, the
driver refuses to load and the row says **No hardware found**. The ordinary
mouse keeps working.

## Good to know

- There is no mouse wheel in os8088, so the wheel does nothing.
- With two monitors in one desktop, the pointer would not line up with the
  mouse, but no emulator that offers this pointer has two displays.
