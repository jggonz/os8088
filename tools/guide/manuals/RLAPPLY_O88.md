# Redline Apply

Redline Apply puts back the bus timing that REDLINE ([[REDLINE.O88]]) tuned
for your machine.
It has no window. You start it, it does its one job in a moment, says what it
did on the menu bar, and is gone.

It exists because a tuned setting does not last. REDLINE's **Tune** tab can
make an AT-class PC's expansion bus faster, so that 8-bit cards such as a
PicoMEM or an 8-bit VGA move their bytes in less time. That setting lives in
the chipset, and the chipset forgets it at every reset or power-off: the BIOS
puts its own timing back. Redline Apply is what puts yours back again.

![Redline Apply in the SYSTEM folder of the system disk.](shot:rlapply-folder){full}

## Starting Redline Apply

Redline Apply is **RLAPPLY.O88**, in the `SYSTEM` folder of the system disk.
Double-click it to run it once.

Usually you do not start it yourself. When you press **Apply on boot** on
REDLINE's Tune tab, REDLINE saves the setting and puts a shortcut,
`RLAPPLY.LNK`, in the `AUTOSTRT` folder of the boot disk. From then on the
machine runs Redline Apply by itself at every start-up.

It needs a 286 or 386 PC with an ISA bus whose chipset REDLINE can tune, and
a setting that REDLINE has saved on that same machine. On an 8088 or 8086
there is nothing to tune, and it says so.

## How REDLINE and Redline Apply work together

1. Open REDLINE, choose the **Tune** tab, pick a **Target** and press
   **Tune**. REDLINE tries the timing, checks that every byte still reads
   back right, and sets the result.
2. Press **Apply on boot**. REDLINE writes the setting to
   `SYSTEM\APPDATA\REDLINE.CFG` on the boot disk, with a fingerprint of this
   machine's BIOS, and adds the start-up shortcut.
3. At every boot, Redline Apply reads `REDLINE.CFG`, checks the fingerprint
   against the BIOS it finds, and writes the setting into the chipset.

![REDLINE's Tune tab, where the setting is made. On this 8088 there is nothing to tune, so Tune and Apply on boot are greyed.](shot:rlapply-tune){full}

The fingerprint is the safety catch. A setting tuned on one board is never
written to a different board, or to the same board after its BIOS has
changed. Redline Apply does not probe or test anything itself; it touches no
hardware until the fingerprint matches.

REDLINE itself never applies a saved setting. Putting it back is Redline
Apply's job alone.

## What it says

Redline Apply reports in one short message on the menu bar, which stays for
about three seconds.

![The message on the menu bar, here on a machine that has never been tuned.](shot:rlapply-toast){full}

| Message | What it means |
|---|---|
| Chipset setting applied | The saved setting is back in the chipset |
| No REDLINE.CFG to apply | REDLINE has not saved a setting on this boot disk |
| REDLINE.CFG is damaged | The file is not one REDLINE wrote, or it is cut short |
| REDLINE.CFG: other BIOS | The setting was saved on a different board or BIOS. Nothing is written |
| REDLINE.CFG: no such row | The file names a chipset this version cannot set |
| RLAPPLY: XT, nothing set | This is an 8088 or 8086 machine, which has no bus timing to set |
| REDLINE.CFG: bad setting | A value in the file is one the chipset does not have, or is below its safe floor |

In every case except the first, nothing is written to the hardware.

## Files

Redline Apply only reads. It looks for `SYSTEM\APPDATA\REDLINE.CFG` on the
disk the machine booted from, which is where REDLINE's **Apply on boot**
puts it. It writes no file of its own.

## Good to know

- To skip it for one start-up, hold **Shift** while the machine boots.
- To stop it for good, delete `RLAPPLY.LNK` from the `AUTOSTRT` folder of the
  boot disk. Deleting `REDLINE.CFG` works too: Redline Apply then only says
  there is nothing to apply.
- If a tuned machine misbehaves, a reset always brings back the BIOS's own
  timing. Skip Redline Apply with Shift, then tune again in REDLINE, or
  choose the **Mfg spec** target, which uses the chipset maker's published
  value.
- Re-tune after a BIOS upgrade. The old setting is refused because the
  fingerprint no longer matches.
