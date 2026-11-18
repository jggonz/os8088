2BASIC FOR OS8088
=================

2BASIC runs numbered BASIC programs in OS8088. This floppy is a B: data
and application disk; it is not bootable. Start OS8088, mount this image
as the second floppy (B:), and launch 2BASIC.O88.

FIRST COMMANDS
--------------
HELP                Show the command index
HELP PRINT          Help for a particular command
PRINT "HELLO"       Run a command immediately
10 PRINT "HELLO"    Enter a numbered program line
20 END              Add an END statement
LIST                Display the stored program
RUN                 Execute it
RESET               Restore startup console colours and appearance
NEW                 Remove the program from memory

PROGRAMS
--------
LOAD "\GAMES\2B99"       Play 99 Things (5 targets)
LOAD "\GAMES\BLACKJAK"   Play Blackjack
LOAD "\GAMES\2BCHECK"   Play the checker game
LOAD "\EXAMPLES\BASICS.BAS"  BASIC language example
LOAD "\EXAMPLES\DRAWING.BAS" Graphics demonstration
LOAD "\EXAMPLES\FORMS.BAS"   Controls and events
LOAD "\EXAMPLES\SOUND.BAS"   PC speaker and optional FM
LOAD "\EXAMPLES\RESDEMO.BAS" Named resources and game data

A folder containing exactly one BAS file can be loaded by its folder name.
If several BAS files are in the folder, specify the full filename. After
LOAD, relative file and resource names resolve from the program's folder.
To stop an executing program, press Esc or Ctrl+C.

RESOURCES
---------
LOADRES loads 2BR3 resource packs with named sprite banks and data.
2BRES.O88 edits 2BR3 files. See RESGUIDE.TXT for instructions.
Existing legacy 2BR2 sprite packs also load.

TESTS
-----
LOAD "\TESTS\AUTOLANG" / RUN  - expect AUTOLANG PASS: 36 CHECKS
LOAD "\TESTS\AUTOR3" / RUN    - expect AUTOR3 PASS: 10 CHECKS
LOAD "\TESTS\AUTOFORM" / RUN  - expect AUTOFORM PASS: 10 CHECKS
FORM and EVENT tests include a small manual interaction test (EVENTUI).

LIMITS
------
The interpreter keeps 288 numbered lines in 4,864 source bytes. It has
one custom form (up to 12 controls), a 35-row text history, and one active
resource pack (8 KB, up to 32 named entries). Resource editing also has
additional limits; see RESGUIDE.TXT.

For the full command reference and build instructions, see the separate
documentation included with the source and GitHub distribution.
