# Application data

`APPDATA` is where programs keep what they write for themselves: high-score
tables, remembered settings, window positions and saved state. It sits
inside `SYSTEM`, out of the way, so none of this clutters the folders you
work in.

## Where it is

Every disk that carries programs has a `SYSTEM/APPDATA` folder, built empty.
A program writes to the one on the disk it was started from. So a game
started from the games disk keeps its scores on the games disk, and the Desk
Calendar keeps its appointments on the system disk.

## Using it

There is nothing to do here: the programs fill it as you use them.

- Keep the disk writable. On a write-protected disk, or a disk with no
  `SYSTEM/APPDATA`, a program still runs, but what it would have saved is
  forgotten when it closes.
- Deleting a file here resets that program: its scores are cleared or its
  settings go back to how they started. The program makes a new file the
  next time it saves.
- Do not put your own documents here. Save them in `MEDIA` or `DOCS`.
