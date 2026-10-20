Synthetic ANSI-BBS fixtures for the os8088 terminal.

WRITTEN BY tools/ansifix.py - never by hand. Change the builder, run
`python3 tools/ansifix.py`, and commit what it wrote; `--check` fails if the
tree and the tool disagree. Nothing here is third-party: every byte is drawn
by a line of Python in that tool, including the one fixture that looks like
artwork.

  <name>.bin   the byte stream a BBS would send
  <name>.txt   what tools/ansisim.py makes of it, for a person: the screen as
               CP437 text, then the attribute plane run-length encoded
  <name>.json  the same for a test: the buffer run-length encoded (`chars_rle`
               and `attrs_rle`, [count, value] pairs), the cursor, what the
               terminal answered, and a SHA-256 of the 4,000 bytes the guest
               holds as char,attr,char,attr,...

A test rebuilds the expected buffer from the .json and compares it with guest
memory. It does not have to run the simulator to do that, which is the point:
the fixture is an oracle, not a cache.
