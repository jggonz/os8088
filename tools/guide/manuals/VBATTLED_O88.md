# Video Battle, with a play record

**VBATTLED.O88** is Video Battle ([[VBATTLE.O88]]) built to keep a record of
each play. It plays a battle exactly as Video Battle does, with the same
window, menu and keys. While it plays it notes how long each frame took to
draw, which frames had to wait for the disk, every read from the disk, and
how the sound kept up. When you leave the battle it writes that record to a
file beside itself.

It is for finding out why a battle stutters or pauses on a particular
machine. Most people will never need it: play battles with VBATTLE.O88.

## Using it

Put it in the same folder as the battle and double-click it, or open the
battle from its **File > Open...**. Play the part of the battle that goes
wrong, then press `Esc` to leave.

The record is written as **VBPROF1.DAT** beside the program. The next play
writes **VBPROF2.DAT**, and so on up to **VBPROF9.DAT**. After nine it writes
no more until you delete or move some, so the disk must not be
write-protected and needs room for them. A record takes up to a few hundred
KB.

The record is not something you open on os8088. Copy it off the disk, with
the battle it came from, and send both to whoever is helping you: the
os8088 project's tools turn it into a frame-by-frame account of the play.

## Where it is

It is on the VGA disk of the demo battle, beside **BATTLEV.VBT**, and in the
Video Battle builder's folder in the tools zip. The builder also puts it on
every hard disk it makes. The other demo disks have no room for it.

## Good to know

- On a machine with extended memory the record is kept there during the play,
  so it takes nothing from the battle's own memory and can be longer. Without
  it the record is kept in ordinary memory and is shorter, and on a machine
  short of memory it may be smaller still, or not kept at all, so that the
  battle can play.
- It needs the same things Video Battle does: a CGA, EGA, VGA or Hercules
  card and the full system disk.
