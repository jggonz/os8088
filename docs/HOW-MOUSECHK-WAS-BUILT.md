# How Mouse Check was built

MOUSECHK - Mouse Check: one window that says what every pointing device
os8088 supports looks like to this machine, and why the one you plugged in was
or was not found.

This is the development diary for a diagnostic package, written as the work
happens and for later reuse on the website. It records what was built and
checked, including decisions that changed the implementation, and keeps what
was intended apart from what was seen. The contract is SPEC.md section 126 and
the story list is `docs/plans/MOUSECHK-PLAN.md`.

## 2026-11-05 - The ask, and the board

The owner's words:

> Use moudiag as a guide to create a package that can help users debug their
> mouse connection. We're receiving reports that PS/2 mouse are not being
> detected in some machines. I want the app to open an interface and show all
> the auto-detected values for all the possible mouse that we support.
> Someone is trying to use a SBC like the one in the picture and can't get PS2
> mouse detected.

The board in the picture is an Advantech half-size ISA single-board computer:
an ST STPC 486-class SoC, a Phoenix BIOS ("D486", 1998), a Winbond W83977AF-A
super I/O whose keyboard controller runs AMI's MEGAKEY firmware ("AM.MEGA
87-96" on the chip), a DiskOnChip 2000, on-board VGA and Ethernet and one
mini-DIN at the bracket. Nothing in `vm/` emulates it, so the first thing the
work had to decide was how a report from it could ever become a fix.

`MOUDIAG=1` (SPEC.md 9.4.6, 9.9.6) already prints what is needed, but it is a
knob kernel: somebody has to build it and send the disk, and the reporter has
a shipped disk. The shipped kernel keeps the boot verdict in the `'MO'` debug
block, but for PS/2 only the handshake step and the BIOS's command byte. So the
package does two things: show what the kernel decided at boot for every pointer
it supports, and, on a button, run the PS/2 handshake again itself with a full
trace. The trace is what turns a field report into a kernel fix.

Three owner decisions shaped everything (plan, O1-O3): the package may read the
kernel's debug registry through ONE named exception to SPEC.md 57.3, it checks
the kernel against the build it was made with and shows raw hex on a mismatch,
and it ships in `SYSTEM\` beside SYSCHECK so it is on the disk the machine
booted from. No kernel byte changes.

## 2026-11-05 - Wave 1: the foundation (shell, row model, kernel reader, shipping)

The work was split so that four section agents could start from one interface.
The foundation is the window and the contract between the window and the
sections.

**The row API is the seam.** A section never sees a window, a pixel or a
segment. It calls `mc_head`, `mc_row`, `mc_line`, `mc_put`, `mc_hex8`... and the
shell rebuilds the whole list from the four sections about every half second.
That is a different shape from SYSCHECK, whose scan is a state machine over
file work; here the work is a few port reads and a few dozen strings, so
rebuilding from scratch is simpler than tracking what changed, and it makes the
sections trivially correct: a section is a function from the machine's state to
a list of rows, with no screen state to keep in step.

**What keeps that cheap is on the drawing side.** The shell keeps the rows being
built (a packed run of records) and, for what is on the glass, one 16-bit hash
per screen line. A rebuild hashes the visible rows and redraws only the lines
whose hash moved, each as ONE opaque `font_run` of fixed width, so the erase is
the padding and no pixel is written twice (SPEC.md 6.1). A heading's lamp is
one opaque 8x8 blit beside its text bar. Keeping a hash instead of a second
copy of the rows is a size decision: two full frames would have taken 10 KB of
a package budgeted at 16 KB; the hashes take 96 bytes. The price is a 1 in
65,536 chance that a changed row hashes the same as the one it replaced and
stays stale until the next change; the refresh is twice a second.

**Reading the kernel is gated by an identity check, not by trust.** SPEC.md 57
says shipped software may not read a debug block, and with reason: a block is
kernel internals with no compatibility promise. The exception MOUSECHK takes is
safe only if it can tell whether the offsets it was written against are the
offsets of the kernel it is running on. The `'ID'` block (SPEC.md 57.6) is
exactly that question, answered by three assembly-time lengths. The build reads
those three words out of the kernel image it just made - at the file offset the
NASM map gives for `kbld_text` - and writes them into the package. No number is
typed. On a match the sections decode the block; on a mismatch they print raw
hex and say why. And `tests/unit/t_mousechk.py` holds the other half: the
offsets in `moblk.inc` against the kernel's own label distances, in the fast
tier, so editing `kernel/mouse.inc` without MOUSECHK fails `make`.

**kern_mega has a different kernel**, so its section lengths and therefore its
`'ID'` differ. The package is not a PKG_MEGA arm (nothing in it changes), but
the fingerprint is part of it, so kern_mega's system disks carry their own copy
built from kern_mega's image - the shape the mega Install package already has.

**Seen on the glass (QEMU, kern_big):** the package's icon in `A:\SYSTEM`, the
window opening on VGA with the Summary heading, its lamp and the Probe button
greyed; a 40-row test section scrolling with PgDn and the bar; Save Report
opening the Save dialog with `MOUSECHK.TXT` in the box, Enter writing the file
(read back off the floppy image on the host: head, date, `Kernel id 9E9D 928A
1789 (the build this package was made with)`, then the rows); Copy toasting.
On CGA (a private `VIDEO=cga` tree) the same window in 11 lines at a 9-pixel
pitch with Probe greyed as a checkerboard. One thing the look caught: a Problem
lamp is a solid square on 1bpp, and next to a black heading bar it merged into
it, so the bar now starts at column 2 where the row labels do and the lamp has a
cell to itself. The sections were stubs, so the test list was a scratch build
and nothing of it is in the tree.

**Mistakes worth keeping.** `mc_head` was also a label inside Pixel's
`pxextra.asm`; nothing assembled them together, but the stack-balance gate walks
every `apps/` file with one flat name table and followed `jmp mc_head` into the
wrong routine. The row API's names are the contract, so Pixel's local label was
renamed (`ex_mchead`, byte-identical output). The button gate wants the
record's declaration in the file that draws the buttons. The toast gate counts
24 characters, which `Out of memory for the report` is not.

**Sizes:** image 4,762 + bss 5,898 = 10,660 bytes of the 16 KB budget; 4,246
packed. Kernel: 0 bytes.
