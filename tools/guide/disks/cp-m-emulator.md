## Using this disk

RunCPM, a CP/M 2.2 machine in a window, with CP/M software to run in it.
Start os8088 from the system disk of the same size in drive A, then put
this disk in drive B, double-click **B:** and double-click **RUNCPM.O88**.
See [[RUNCPM.O88]].

The folder `A` is CP/M's drive A, and the numbered folders inside it are
its user areas. Area 0 holds RunCPM's master disk: the CP/M tools, MBASIC,
the assemblers and the debuggers. To reach the software in another area,
type `USER 5` (or 6, 7, 8, 9) at the `A>` prompt. `GAMES.TXT` in area 0
lists what this disk carries.

## What each size carries

| Size | Beside the master disk |
|---|---|
| 1.44MB | Area 5, the Yahoo arcade games (LADDER, CATCHUM); area 6, the dungeon games (NEMESIS, Dungeon Master, CASTLE); area 7, GAINA; area 8, Turbo Pascal 3.01A; area 9, WordStar 3.30 |
| 1.2MB | All of the above except Turbo Pascal |
| 720KB | Area 5, the arcade games |
| 360KB | Nothing, and a smaller master disk with its source and document files left out |

Run a program's setup once before it will draw (LADCONF, CATCONF, TINST,
WINSTALL) and choose **10) Heathkit/Zenith H19 (ANSI)**.

Leave the disk writable: CP/M programs save their files in `A`.
