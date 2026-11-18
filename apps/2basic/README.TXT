2BASIC FOR OS8088
=================

2BASIC is a small BASIC interpreter for OS8088 on 8086 and 8088 PCs.
It provides numbered BASIC programs, graphics, forms, events, sound
and named resource files. 2BRES is the included resource editor.

Developed using OpenAI CODEX as part of an AI engineered development workflow.

TO RUN
------
1. Boot OS8088 in 86Box or on compatible hardware.
2. Mount 2BASIC-APPS360.IMG as the B: floppy (not the boot disk).
3. Open B: in the OS8088 file manager and launch 2BASIC.O88.
4. Type HELP for the command index, or try:

   PRINT "HELLO"
   10 PRINT "HELLO"
   20 END
   LIST
   RUN

Five short programs are in EXAMPLES on B: (BASICS, DRAWING,
FORMS, SOUND and RESDEMO). For example:

   LOAD "\EXAMPLES\BASICS.BAS"
   RUN

To play the games:

   LOAD "\GAMES\2B99"
   RUN

   LOAD "\GAMES\BLACKJAK"
   RUN

A program's folder becomes its working directory, including for RES
files. Use RESET to restore the startup console and directory, and NEW
to erase the stored program. Escape or Ctrl+C interrupts a running
program.

RESOURCE EDITOR
---------------
Launch 2BRES.O88 from B: to create or edit named 2BR3 files.
Documentation and sample images are included on the disk.

SOURCE BUILD
------------
In the os8088 tree: `make 2basic` builds 2BASIC.O88 and 2BRES.O88
against apps/os88api.inc, and `make 2basicdisk` the B: disk in all
four floppy geometries (SPEC.md section 132). Upstream's MAKE.BAT,
MAKEDISK.BAT and pinned SDK download are not used here.

DOCUMENTATION
-------------
  docs/USERGUIDE.TXT   Running 2BASIC, examples and limits
  docs/COMMANDS.TXT    Detailed command reference
  docs/RESOURCES.TXT   Resource editor and formats
  docs/TESTING.TXT     Release acceptance tests
  docs/DEVELOP.TXT     Building and contributing (upstream's source package)

This disk is not bootable and does not contain OS8088 itself.
