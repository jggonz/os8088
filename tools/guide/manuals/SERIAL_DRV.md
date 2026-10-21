# Serial ports

`SERIAL.DRV` lets desktop programs exchange data through a serial cable
while the serial mouse keeps working. WireFleet uses it to connect two PCs.
It is loaded when a program connects and released when it disconnects.

Keep this hidden system file beside the other system files on the disk
you booted. There is no window or Control Panel setting for it.

Connect the computers with a null modem cable. A program can look for
the free port by itself, as WireFleet does, or let you choose COM1 or
COM2. If the mouse has not moved since the machine started, move it once
so the system can tell it from the cable. A port already used by the
mouse or another program is refused. The program chooses the speed:
WireFleet uses 2400 baud, eight data bits, no parity, one stop bit and no
flow control.

Disconnect serial programs before hibernating. Closing a connected
program releases its port, including when the program is stopped by
Task Manager.
