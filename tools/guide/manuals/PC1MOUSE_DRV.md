# Olivetti PC1 mouse driver

`PC1MOUSE.DRV` adds support for the built-in mouse on an Olivetti Prodest PC1. It uses the normal os8088 pointer and clicks.

## Using the PC1 mouse

Boot from the PC1 system disk with this driver. Open the **Control Panel**, choose **Drivers**, and tick **Olivetti PC1 mouse**. Close the Control Panel to save the choice for the next boot.

This driver is for the Olivetti Prodest PC1's built-in mouse hardware. It shares the mouse-driver slot with **USB Mouse**, so only one of those drivers can be loaded at a time. Leave **USB Mouse** unticked on a PC1.

The driver is optional on other computers. If the PC1 hardware is not detected, it stays unloaded and the other available mouse continues to work.
