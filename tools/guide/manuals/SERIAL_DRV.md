# Serial ports

`SERIAL.DRV` lets desktop programs exchange data through a serial cable
while the serial mouse keeps working. WireFleet uses it to connect two PCs.
It is loaded when a program connects and released when it disconnects.

Keep this hidden system file beside the other system files on the disk
you booted. There is no window or Control Panel setting for it.

Connect the computers with a null modem cable. Wait for the serial mouse
to be detected, then choose the other port in the program. A port already
used by the mouse or another program is refused. The connection uses
9600 baud, eight data bits, no parity, one stop bit and no flow control.

Disconnect serial programs before hibernating. Closing a connected
program releases its port, including when the program is stopped by
Task Manager.
