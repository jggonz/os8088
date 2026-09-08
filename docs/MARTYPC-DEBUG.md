# The MartyPC debugger — an instrument the guest cannot feel

**What it is.** A remote debug server bolted into [MartyPC](https://github.com/dbalsom/martypc)'s
headless frontend, and a host client. It gives a process on your machine
memory, registers, I/O ports, breakpoints, single-step and cycle counts on a
running os8088, with **no code in the guest at all** — no driver, no UART, no
interrupt, not one guest cycle spent. `tools/martypc/` is the whole of the
change to MartyPC: `debug_server.rs`, four patches, the machine configs and
`build.sh`.

**It is pinned and static, on purpose** (`tools/martypc/UPSTREAM`: MartyPC
0.4.2, commit `e15cb04f`). A debugger that changes under you is one more
variable in a session whose whole point is removing them; re-pinning is a
deliberate act, not maintenance.

**THIS IS WHAT YOU DEVELOP ON.** Anything that runs on an 8088 — the whole of
this OS bar the 286/386 targets, including **all three** of SPEC.md §39's
adapters, input, screenshots and sound — is tested here. QEMU is a fallback
with a **six-item list** (docs/TESTING.md states it: 286/386, rung 1 of the
hard-disk driver, SPEC.md §9.5's awkward mouse cases, the PS/2 mouse, the
Ethernet card, the RTC's write half), stated as a list so that "a legitimate
need" is something you can check rather than argue yourself into. The 5150
remains where a number with a disk in its timing LANDS, though the floppy
model below agrees with it to the measurement quantum.

`make marty` builds from source with cargo — a couple of minutes in a
container with a warm cargo registry, longer cold. **Build it at the start of
a session rather than when you first need it**, because the moment you first
need it is the moment the cost feels like a reason to type `make test`
instead. **A run past ~180 s has frozen rather than slowed** — the guest runs
at several times real time, so the overrun is the finding; diagnose it rather
than raising the timeout.

---

## Build and run

Needs `cargo` (Rust) and, on Linux, `libudev-dev` + `pkg-config` — MartyPC
depends on `serialport`, whose build script hard-fails without them.

### Installing the deps in a fresh Ubuntu container

**This subsection is about ONE environment**: a fresh Ubuntu container, which
is what an agent session gets. A Mac has neither problem — `tools/setup-macos.sh`
installs through Homebrew (but not Rust, so `make marty` there wants `cargo`
put in front of it by hand).

**`apt-get update` FIRST.** The shipped index names a `libudev-dev` that has
been superseded and removed from the pool, so installing it straight off
404s. A refresh is the whole fix.

**...and if `apt-get update` says**

```
E: gpgv, gpgv2 or gpgv1 required for verification, but neither seems installed
```

while `/usr/bin/gpgv --version` answers perfectly well, the missing thing is
not gpgv: apt drops to the unprivileged `_apt` user to fetch and verify, and
in a container whose filesystem that user cannot traverse the check fails
with that sentence. The tell is a plain `apt-get update` that ends in
`W: Some index files failed to download` having touched nothing, after which
the install 404s exactly as it does with no refresh at all. Run both steps
with the sandbox off:

```sh
apt-get -o APT::Sandbox::User=root update
apt-get -o APT::Sandbox::User=root install -y --no-install-recommends \
        libudev-dev pkg-config
```

Do not pin a version here. Skipping the deps does not fail at apt: it fails
minutes later inside cargo, on `serialport`.

**`qemu-system-x86` fails the same way and needs the OPPOSITE fix.** The
index lists the `noble-updates` build, whose `.deb` 404s on
`archive.ubuntu.com` and then times out against `security.ubuntu.com`, so a
plain install burns several minutes and fails. Pin all three packages to the
**base** noble version:

```sh
V='1:8.2.2+ds-0ubuntu1'              # the BASE version, NOT -updates
apt-get install -y --no-install-recommends \
        "qemu-system-x86=$V" "qemu-system-common=$V" "qemu-system-data=$V"
```

`-t noble` is **not** enough — it still resolves to the `-updates` version.
`--no-install-recommends` skips the gstreamer/libcaca display extras, which
404 the same way and which a headless `-display none` run never touches.

So: **`libudev-dev` wants the NEWER version a refreshed index names; QEMU
wants an OLDER one than the index names.** Applying either cure to the other
package reinstates the 404 you are trying to escape.

If a previous attempt is wedged, clear `/var/lib/dpkg/lock-frontend` and run
`dpkg --configure -a` first — and **do not `pkill -f apt-get` from inside a
Bash tool call**, because the pattern matches the calling shell and kills it.

### By hand

```sh
tools/martypc/build.sh              # clone at the pin, patch, stage, build
cd build/martypc/run
MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --mount fd:0:media/floppies/os8088-360.img &

python3 tools/os88marty.py 127.0.0.1:9001 run
python3 tools/os88marty.py 127.0.0.1:9001 status
```

`--mount fd:N:path`, not `floppy:` — the device word is `fd`, `hd` or `cart`.
`make marty` copies `build/os8088-360.img` into
`build/martypc/run/media/floppies/` for you. `--machine-config-name <name>`
picks a machine (the table under *The machines*); `--turbo` is the 7.16 MHz
control.

**The machine starts PAUSED**, whatever `auto_poweron` says, and `run` starts
it. A debugger that attaches to a machine already millions of cycles into its
boot cannot breakpoint anything it wanted to watch.

### The IBM ROM is not in the tree

`tools/martypc/roms/` is gitignored and ships empty: the 27 OCT 82 5150 BIOS
is IBM's and cannot be redistributed under this tree's licence. `build.sh`
names the file and its md5 (`BIOS_IBM5150_27OCT82_1501476_U33.BIN`, 8192
bytes, `f453eb2df6daf21ec644d33663d85434`) when it does not find one. Without
it every machine whose `rom_set` is `ibm5150_82_v4` exits at once with
`Error loading ROM set: ibm5150_82_v4 not found in ROM set map` — **and that
includes `os8088_5150_cga`, the default machine of both `os88marty.launch()`
and the `os88marty.py launch` verb.** In a checkout without the ROM, name a
GLaBIOS machine (`--machine os8088_5150_cga_gla`), or go through
`os88marty.machine(name)` / `os88ui.boot()`, which resolve an IBM-romset name
to its GLaBIOS twin. `machine(name, why_ibm="...")` is the form for a row
that genuinely needs the period ROM: it raises when the ROM is absent, so the
row SKIPS instead of quietly running on the twin.

### From a script: `os88marty.launch`

Do not hand-roll the above in Python. Every scripted session needs a fresh
emulator, and none of the failures in those twenty lines announce themselves.

```python
import os88marty
from os88mouse import Mouse

with os88marty.launch("build/os8088-360.img",
                      apps="build/apps360.img",
                      machine="os8088_5150_cga_gla") as m:
    mo = Mouse(marty=m)                 # ONE connection, shared
    mo.dblclick(608, 105)
    os88marty.settle(m)                 # ...instead of time.sleep(4)
    m.vram("cga")
```

`launch(image, apps=None, machine="os8088_5150_cga", addr=None, run_dir=None,
boot=True, timeout=..., extra=(), card=None, label=None, detach=False)`.
`boot` is True to run until the desktop is up, a number of seconds to run
blind, or 0 to return the machine paused.

- **Every instance is isolated, and it takes no argument.** `addr=None`, the
  default, has the emulator ask the OS for a free port under its own bind and
  publish what it got, and the machine runs in a private directory under
  `build/martypc/inst/`. Two of these in one checkout do not see each other.
  The address is on the object afterwards (`m.addr`, `m.port`), which is
  what to hand to `tools/os88mouse.py`. Pass `addr=` only to pin a port on
  purpose.
- **It reaps orphans and nothing else.** An emulator whose owning script died
  is killed by PID; a live instance with a live owner is left alone.
  `kill_all()` — `os88marty.py kill-all --yes` — is the sweep, kept as a
  deliberate act and never automatic.
- **Never `pkill -f martypc_headless` and never `pgrep -f`.** The pattern
  matches the calling shell's own command line, so `pkill` can kill the
  caller and `until ! pgrep -f …` never finishes — and it would kill
  everybody else's instance, which is the damage this layer exists to
  prevent.
- **It owns the process.** `close()` — or leaving the `with` — kills it, on
  the failure paths too. It takes the instance's media with it, so read a
  disk *inside* the `with`, not after.
- **It checks the PROCESS, not just the cycle count.** `ping` reports the
  emulator's pid and `launch` compares it against the one it spawned:
  `cycles == 0` says the machine has not run yet, which a stale emulator
  paused at the start of its own boot also says.
- **Each floppy is copied into the instance's own run directory.** The guest
  WRITES to a mounted image (`SYSTEM.CFG`, saved files), so a run would
  otherwise dirty `build/`. A machine with a hard disk gets its own clone of
  the VHD for the same reason.
- **`os88marty.py instances` is the first thing to type** when a session
  behaves as though something else is driving the machine. It lists what is
  running, whose it is, and whether the owner still exists; `reap` clears the
  orphans; `kill <port>` ends one; `kill-all --yes` is the hammer.

```
$ python3 tools/os88marty.py instances
PORT   PID     MACHINE                OWNER      AGE     LABEL
38193  13983   os8088_5150_cga_gla    13979 running  12s   dispdrag.py
43121  13987   os8088_5150_cga_gla    13979 running  11s   dispdrag.py
```

#### `settle(m)` — the wait, and the boot gate

`settle(m, quiet=1.0, stable=2, gate=None, limit=120.0, card=None,
guest=None)` replaces every `time.sleep(4)`: it returns once `stable`
rendered frames `quiet` seconds apart are identical, which an os8088 screen
only is between events. `launch` uses it with a gate for the boot, and the
gate is not optional — the two obvious ones are both wrong:

- **Stillness alone returns during the BIOS POST**, which sits perfectly
  still for seconds before the floppy is touched (measured: an 8.3-second
  "boot" showing a quarter of the desktop's lit pixels).
- **"Has the card left text mode" hangs on Hercules.** MartyPC's MDA reports
  text mode forever, in graphics mode as in any other.

So the gate is the **desktop**, sampled through `vram` on the 1bpp cards and
`fbuf` on VGA, and it is THREE facts: the menu bar's white field, the 1px
black rule under it (SPEC.md §12), and the dock strip — the first thing on
the screen and the last. The rule is what rejects POST text, the one screen
whose top band is genuinely lit. Measured from reset, field / rule / dock:

| | field | rule | dock |
|---|---|---|---|
| POST text | 0.26 | **0.25** | 0.25 |
| splash | 0.00 | 0.00 | 0.00 |
| CGA desktop | 0.93 | 0.00 | 0.96 |
| Hercules desktop | 0.94 | 0.00 | 0.96 |
| VGA desktop | 1.00 | 0.00 | 0.96 |

**The gate and the stillness test read the screen ONCE, together.** The
emulator runs the guest several times faster than real time, so a round trip
is tens of milliseconds of *guest* time — most of a desktop paint on a
4.77 MHz machine — and two reads one round trip apart can report a state that
never existed (a probe built that way reported the menu bar up while the same
screen was 26% lit). **When the host is fast, two questions asked separately
are two questions asked about different machines.**

**On a Hercules, `fbuf` is cropped: guest (x, y) renders at `fbuf`
(x−16, y+2)**, over a 720x350 window on a 720x348 framebuffer. Measured twice
independently (2,280 of 2,280 sampled pixels agree at that offset and at no
other; a second correlation over 4,992 samples put the mismatch at 0.0000 at
dx = −16, dy = +2). VGA comes back 640x480 at (0, 0) and CGA at (0, 0), so
two adapters of three encourage the assumption the third breaks. A pixel
gate that compares `fbuf` against anything else needs both halves.

**It bites as a DEFECT REPORT, not as a shear**, which is why it is worth a
paragraph rather than a footnote. Clear Skies spent a session chasing "94 lit
pixels beside the view" that were the view's own leftmost sixteen, read at a
box column they do not occupy; the shadow was clean and the spans were the
view's, and both readings were taken as *the bleed comes from somewhere else*
rather than as *there is no bleed* (SPEC.md 88.13.4.1). A band named in the
guest's coordinates is off by exactly two bytes here, which is the size of a
plausible bug. **On the 1bpp adapters read `m.vram()`** — the card's memory,
byte for byte the arithmetic in `tools/hercshot.py` — and keep `fbuf` for
VGA, where it is the only route.

**`card=` is not optional on a two-card machine.** `settle`, `launch` and the
screen probe ask `video` with no card by default, which answers MartyPC's
**primary** — the first `[[machine.video]]` block — and os8088 need not be
driving it. The boot gate then watches a card nothing is drawing on and
times out after 120 s saying *"this machine never finished booting"* about a
machine that booted fine. Pass `launch(..., card=1)`; a caller running
`settle` itself passes `gate=desktop_up, card=<idx>`. `advance(frames=…)`
takes it too, and there it decides which card's 50 Hz or 60 Hz is counted.

Boot times are a property of the HOST (measured here: CGA 4.6 s, Hercules
4.7 s, VGA 7.1 s; three to four times that on a slower container), which is
exactly what `settle` exists so that nothing has to hard-code.

**`quiesce(m, read, guest=0.5, stable=2, ...)`** is `settle`'s shape applied
to a few bytes of guest memory instead of a framebuffer, over GUEST seconds:
identical readings of `read(m)` a fixed guest interval apart. For a wait
that is followed by a memory read and no pixel comparison, it is the cheaper
instrument (docs/plans/SOAK-PARALLEL.md §11).

### Start at `tools/os88ui.py`, not at the mouse

The mouse drivers below are the layer under this one. What a test usually
means is *"open the Disk window on B:"*, not *"double-click (584, 48)"* — and
`tools/os88ui.py` is that vocabulary, resolved out of the guest's own tables
and **confirmed by reading guest state**:

```python
import os88ui

with os88ui.boot("build/os8088-360.img", apps="build/apps360.img") as ui:
    disk = ui.open_drive("B")            # ...whatever ordinal B: has today
    w    = ui.path("APPS/CALC.O88")      # ...scrolling if the row is below the fold
    ui.drag_window(w, 20, 12)            # ...and checking where it landed
    ui.raise_window("APPS")
    ui.menu_pick("Calc", "Close")        # ...off menu_bar[], not four numbers
```

`boot(image, apps=None, machine="os8088_5150_cga", card=None, saver=False,
why_ibm=None, verbose=True, limit=180.0, **kw)` is `launch` plus the two
things most scripts forget: the machine name goes through
`os88marty.machine`, so an IBM-romset name resolves to its GLaBIOS twin, and
**the screen saver is turned off by default** — five guest minutes of no
input is reachable on a busy lane. The verbs: `open_drive`, `open`, `path`,
`window`, `wait_window`, `raise_window`, `close`, `move_window`,
`drag_window`, `uncover`, `clear_desktop`, `disk_window`, `listing`,
`scroll_to`, `menus`, `menu_pick`, `toast`, `wait_toast`, `settle`.

**Confirming is FASTER than not confirming.** `settle` cannot know what it is
waiting for, so it waits for the whole screen to go quiet and then `quiet`
seconds more. Reading `wm_wins` to see whether the window has opened is a
408-byte read that answers the actual question and returns the instant it
is yes. Measured by `tests/uilayer.py`, the same three-step navigation:

| | host | guest |
|---|---|---|
| double-click, `settle`, repeat | 15.0 s | 250,573,106 cycles (52.5 s) |
| the same through `os88ui` | **5.2 s** | **87,422,885 cycles (18.3 s)** |

`ui.settle()` is still there for a test that compares PIXELS.

**A verb that cannot do what it was asked RAISES, naming what it saw** —
`no window called 'MINES' is open. What is open: ['Disk', 'APPS']` — where a
click that lands on bare desktop is reported twenty steps later wearing the
costume of the feature under test. The cases it handles:

- **a drive with no zone.** Zone position is the volume's place among the
  *shown* ones, so a machine whose B: was retired by §18.97's probe numbers
  them differently. `open_drive` walks `dsk_vtab` and says so instead of
  clicking bare desktop.
- **a row that is not a row.** §19.4 sorts by name, so a folder that gains an
  entry renumbers every row after it, and one that outgrows the window puts
  the row below the fold. `open` looks the name up, scrolls by reading
  `FS_SCRL` back, and checks the row before it clicks.
- **a title bar that is covered.** `raise_window` walks the z-order for a
  column nothing covers, and falls back to the dock tile, which is always
  visible (§30).
- **the acting Disk window is not the front window.** `fm_vp_set` runs on a
  file-manager raise and its navigations; nothing calls it when a Calculator
  comes forward, so `[fm_vp]` goes on naming the last Disk window. A row
  position computed off the front window then lands inside the Calculator.
  `ui.disk_window()` resolves it through `[fm_vinst] → I_WIN`, and `ui.open`
  raises it before scrolling, because the arrow keys only reach the
  frontmost window.
- **§11.94 snaps a dragged window's content origin to a multiple of 8**, so
  a window dragged to *x* lands at `((x + 1) & ~7) - 1` (`os88geom.snapx`);
  a check written against the requested x fails on a window manager doing
  exactly what the spec says.
- **a menu item carrying §12's `MENU_DIS` prefix cannot be selected**, so a
  drag aimed at one releases over its neighbour. `menu_pick` refuses it
  before the press, and checks `menu_sel` **before the release**.

Every constant it uses comes from `tools/os88geom.py`, which checks itself
against the kernel source at import and is checked again by
`tests/unit/t_mirror.py` in the fast tier. `os88geom` decodes BOTH kernels'
layouts; a kern_small script needs `$OS88_BUILD`/`$OS88_DEFINES` pointing at
that build or `wm_wins` is decoded at the wrong stride.

### Which mouse driver — there are two, and picking wrong fails silently

| | | |
|---|---|---|
| **`tools/os88mouse.py`** | **ABSOLUTE — the default** | Reads the kernel's published `mouse_x` (SPEC.md §9.4.3), computes the exact remaining delta and **proves arrival**. `Mouse(marty=m)`, then `where` / `to` / `click` / `dblclick` / `drag` / `menu`. A target it cannot reach raises; it never clicks into empty desktop. |
| **`tools/os88mouserel.py`** | RELATIVE | `Rel(m, pace="frames")`: dead reckoning off a corner pin (`home()`), guest-**frame** pacing for a bit-exact replay, proven button edges (`press`/`release`), and `drift()`/`check()` to say when reckoning has come apart. CLI verbs `packet`, `move`, `home`, `press`, `release`, `drift`. |
| `Marty.mouse()` in `tools/os88marty.py` | the transport | One 3-byte Microsoft packet into the emulated UART. The layer both drivers sit on, and correct to call directly **only** for packet-level work. |

**Use the absolute one unless your case is on this list**, which is the whole
of it:

1. **the mouse itself is under test** — packet decoding, SPEC.md §9.5's port
   contest, the ISR's own stack (§9.10). Asking the kernel where the arrow is
   would be asking the thing under test to mark its own work;
2. **a replay must be reproducible** (docs/plans/completed/SNAPSHOT-PLAN.md
   §7) — the absolute driver sends however many packets convergence needs,
   and how many that is depends on the host;
3. **the motion has no destination** — a paint stroke, a window drag, a sweep.

The failure shape is why this needs writing down. A dead-reckoned click lands
three pixels outside a 16-pixel control, **nothing happens, and no error is
raised**. A packet carries a signed byte per axis, the 1200-baud UART drops
one sent while the previous is in flight, and the kernel's edge clamp eats
overshoot; those three are why aiming by hand does not work.

```
python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90   # NOT two clicks
python3 tools/os88mouse.py 127.0.0.1:9001 where
```

### Several at once

**Two agents, two terminals or two rows of the suite can drive MartyPC in one
checkout, and nothing has to be arranged between them.** Three things are
private per instance:

| | |
|---|---|
| the port | **the OS picks it under the emulator's own bind** (`MARTYPC_DEBUG_ADDR=…:0`) and the emulator writes it to `MARTYPC_DEBUG_PORTFILE`. Probing for a quiet port from the client and then launching is a race — two launchers a millisecond apart pick the same port — which is why this is in `debug_server.rs` and not the wrapper |
| the run tree | one directory per instance under `build/martypc/inst/`, read-only parts symlinked, floppies, VHDs and the log real and private |
| the process table | a registry per instance, and `reap()` kills only what nobody owns |

```python
with os88marty.launch(IMG, label="herc") as a, \
     os88marty.launch(IMG, machine="os8088_5150_herc_gla", label="cga") as b:
    ...                             # two machines, no arrangement
```

Three failures are gated rather than left to be discovered:

- **A second client on one instance is refused in words.** The server
  accepts and answers `{"ok": false, "busy": true, …}` naming the client that
  holds it, and `Marty` turns that into one sentence: *this debug server
  already has a client (127.0.0.1:35214); it takes one at a time. Share that
  connection, or launch a second emulator of your own.* Sharing is the
  answer within a session: `Mouse(marty=m)`, `Flush(marty=m)`.
- **A bind that fails is fatal and says so on stderr**, not only through
  `log::`, whose level is a config away from being off. An emulator nothing
  can talk to must not keep running: it holds the port against the next run.
- **A port somebody else holds is an error that names them.** `launch(addr=…)`
  checks the registry first, so pinning a port on purpose cannot silently
  attach to the wrong machine.

#### How many, and what stops you

**There is no hard cap** — a refusal would be a new way to lose work. There
is one line on stderr when the count goes past the box's core count
(`OS88_MARTY_MAX` moves it), read as the narrowest of the hardware, this
process's affinity and any CFS quota. Aggregate guest speed against a real
4.77 MHz 8088, on a four-core container:

| instances | per instance | aggregate |
|---|---|---|
| 1 | 3.42x | 3.4x |
| 2 | 3.37–3.48x | 6.9x |
| 4 | 2.96–3.43x | **13.1x** |
| 6 | 1.96–2.88x | 13.9x |
| 8 | 1.64–1.71x | 13.4x |

**Flat past the core count**: the ninth instance does not make the box do
more work, it makes the other eight slower. Nothing else binds first — an
instance is ~50–100 MB of RSS and ~1 MB of disk (the 32 MB VHD is reflinked
where the filesystem can).

**Going past the ceiling is not broken, only slower.** Guest **cycle**
counts, `disk()` counts and pixel comparisons are unchanged at any
oversubscription, being counted rather than timed. What loses slack is
host wall-clock: `settle`'s patience, an `until` limit, a suite row's
timeout (4x its declared seconds). Four emulator rows through
`os88test.py`: 175.6 s at `--marty-jobs 1`, 85.9 s at 3, each row 6–10%
slower and none failing; three rows with single-figure host-second windows
(`tests/bouncecost.py`, `tests/modstr.py`, `tests/paintcull.py`) all passed
at eight instances on four cores. An IDLE guest is cheaper than the table
says — an os8088 desktop is 96.9% halted (SPEC.md §8.1.2) and halted cycles
are cheap to emulate — so "eight agents" is the worst case only when all
eight are driving.

#### A record names a PROCESS, not a PID

`pid_max` in a container is 32,768, so a session launching emulators
steadily wraps the PID counter in minutes. A registry record that carried a
bare PID once matched somebody else's live instance and `reap()` killed it.
A record is `(pid, start time)` — field 22 of `/proc/<pid>/stat`, or `ps -o
lstart=` on Darwin — and one predicate, `_killable`, gates every signal: the
record must carry a start time, that PID must be a `martypc_headless`
started at that time, and the record must not already be retired. A record
that cannot identify its process is left alone, record and process both.
`tests/martyconc.py` (full tier) fabricates both shapes against a live
instance and asserts it survives.

#### A bench that outlives the command

A session that boots once and then pokes at the machine from several
separate commands — an agent at a terminal, `tools/notepad/` — wants an
emulator that is nobody's to close. `launch(detach=True)` is that, and the
CLI wraps it:

```
$ python3 tools/os88marty.py launch build/os8088-360.img --apps build/apps360.img --machine os8088_5150_cga_gla
127.0.0.1:33689
  pid 14625, os8088_5150_cga_gla, log build/martypc/inst/14621-bench-1/martypc.log
  it OUTLIVES this command. `os88marty.py instances` lists it, `os88marty.py kill 33689` ends it.

$ python3 tools/os88mouse.py 127.0.0.1:33689 dblclick 608 105
$ python3 tools/os88marty.py 127.0.0.1:33689 shot /tmp/desk.png --rendered
$ python3 tools/os88marty.py kill 33689
```

`launch` takes `--apps`, `--machine`, `--label`, `--no-boot` and `--card`.
A detached instance has **no owner on purpose**, which is what distinguishes
it from an orphan — `reap()` leaves it alone for ever and `kill` is how it
ends. A bench you forget about is a core you have lost until you
`instances` and notice it. The bench's run directory is the one in the log
path it prints; `tools/os88flush.py` needs it as `--run-dir` (below).

### `until(m, cond, what)` — the wait for work that draws nothing

`settle` watches pixels, so it is **silently wrong for anything that holds
the gfx lock for its whole run**. A hard-disk install freezes the UI while it
copies: the screen is *more* still while it is busy than when it is done, so
`settle` returns about five seconds in and a `with launch(...)` block then
kills the emulator mid-copy — which looks like the installer stopping
halfway.

Ask about the thing instead. `cond` is called with the Marty each round and
may look wherever the answer is — guest memory, or the **host** side of a
mounted image, which is usually better because a commit tends to be one
write you can watch for:

```python
STUB = bytes.fromhex("fa31c08ed88ec08ed0bc007c")   # hd_bootstub's opening
os88marty.until(m, lambda _: open(vhd, "rb").read(12) == STUB,
                "the installer to commit the MBR")
```

SPEC.md §52.10 writes the partition table **last**, as the commit, so that
one comparison is an exact "the install finished".

`until(m, cond, what, poll=1.0, limit=600.0, guest=None)` separates the two
ways the wait fails. A guest that has **stopped executing** can never
satisfy any condition, so it says which state and where — `the guest is
'breakpoint' at 0060:3C21 and is not executing` — rather than blaming the
condition; that is the shape a still-armed breakpoint takes. Everything else
is an honest timeout: the guest is still running, so the limit is too short
or the condition asks the wrong thing.

**Pick by whether the screen is the evidence**: `settle` for a boot, a click
or a repaint; `until` for a format, a copy, an install, a save.

**Widening `settle`'s window to cope is the trap on the other side, and it
raises.** `settle(m, quiet=30, limit=2400)` is unsatisfiable: `stable`
samples `quiet` apart is `stable * quiet` **host** seconds of unchanged
screen, the menu bar's clock changes once a **guest** minute, and the guest
runs faster than real time, so no two samples can ever agree. It waits out
the limit and blames the guest (an install driven that way sat for 40
minutes with the install long finished). `settle` samples the cycle counter
first and refuses a window it can prove cannot close, naming `until`.

### Naming a kernel flag: `os88sym`

`m.sym("fpg_on")` is the flat address of a kernel symbol, and `python3
tools/os88sym.py --all` lists every one. **Do not take an address out of
`nasm -l`'s listing** — for anything in `.bss` both the address column and
the bracketed operand bytes are section-relative and fixed up afterwards, so
`menu_bovr` reads as `0x0879` there and is at `0xCBA4` in the binary. That is
a plausible small number pointing into `.text`: reading a byte from it
succeeds, returns something, and means nothing.

`os88sym` re-assembles a temporary copy of `kernel/kernel.asm` with
`[map all]`, attributes every symbol to its section — `.text`/`.bss` at
`KERNEL_SEG`, `.cold` at `COLD_SEG`, `.lowbss` at `LOW_SEG`, stage 2's blob
where `[spl_fseg]` says — and asserts the result is byte-identical to
`build/kernel.bin`, so a map describing a different kernel is an error
rather than a subtly wrong answer. A knob build moves everything: pass the
same `-D`s (`--define DISKCNT=1`, or `$OS88_DEFINES`), and a kern_small
build wants `$OS88_BUILD=build/smallk`. **Committing moves three bytes of
`.text`** (the About box's build number is the commit count, SPEC.md §14.2),
so `make` after a commit or every row dies saying the map describes a
different kernel.

---

## The disk has a platter

**Stock MartyPC does not model a floppy's mechanics**: `operation_read_data`
streams a whole run to DMA as fast as the CPU can turn, a seek returns
`CommandComplete` in the breath it is issued, and `media_geom`'s
sectors-per-track is hardcoded to 0. That is why it read a 16 KB file ~30x
faster than the 5150 (PERFORMANCE.md Part 9 Set 11).
`tools/martypc/patches/04-floppy-disk-timing.patch` gives it one.

### What it models

| | |
|---|---|
| **Rotation** | a head angle per drive, advanced while the motor turns. 300 RPM (360K/720K/1.44M drives) or 360 (a 1.2M), so a revolution is 200 ms or 167 |
| **Data rate** | 250 kbit/s DD, 500 HD, and **300** for DD media in a 1.2M drive. 32 us a byte at 250, so a 512-byte sector's data field is 16.384 ms |
| **Interleave** | the physical order of the logical sectors round the track, from the machine config (`interleave`, default 1) — a raw sector image cannot supply it. **No os8088 machine sets one**: the field 5150's media is 1:1 |
| **Pacing** | one sector at a time: wait for it to come round, stream it, wait for the next — a real controller DRQs as each sector arrives and pauses only over the gaps |
| **Seek** | a step per cylinder crossed at the rate the BIOS asked for through SPECIFY, and **no settle** — the settle is the BIOS's own software wait, and charging it here counted it twice (Set 37). The platter keeps turning while the head steps |

The 5150's three raw `int 13h` rows (Sets 14 and 22) are one fact seen three
ways — a sector is readable only as it passes the head — and the model
reproduces them without being told any of them:

| | field, IBM 5150 | this model |
|---|---:|---:|
| one sector, re-read | 199,106 us | **199,106** |
| a 9-sector track, one call | 398,211 us | **384,480** |
| the same nine, as nine calls | 1,991,057 us | **2,004,789** |

Rows 2 and 3 are one measurement quantum out — 13,731 us, `bl_run`'s tick
over the row's four iterations. A 9-sector 1:1 track is **one** revolution of
transfer and both machines take **two**: the missing turn is the IBM ROM
asking for the diskette parameter table's head settle and spending 52.5 ms
on it in a `LOOP $` at `F000:EEB8`, once per `int 13h`. MartyPC reproduces
that by running the ROM (Set 37). The seek's step rate is 8.00 ms a cylinder
against the field's 7.81 (Set 36), and five of `sysbench`'s six seek rows are
exact; the 39-cylinder one is a tick short.

**The like-for-like boot** (`combo.img` on `os8088_5150_herc`) is **188
`boot_ticks` against the field's 205** — 0.92x. The same image boots
`os8088_5150_cga_gla` in ~175 and QEMU cannot be compared at all.

### What it does NOT fix, and this is the half that matters

**It changes what the disk COSTS, not what it SAYS — and the BIOS is not
modelled, it is EXECUTED.** With the real ROM MartyPC runs IBM's own
`int 13h`, so a bug in that code is present by construction. SPEC.md
§18.91's `AL` bug reproduces here: same image, `os8088_5150_herc`, shipped
kernel against `make DISKAL=1` —

| | shipped (trusts `CF`) | `DISKAL=1` (trusts `AL`) |
|---|---|---|
| int 13h-level reads | 24 | **183** |
| sectors moved | 183 | **870** — 4.75x |
| **longest run** | **9** | **9** |
| `boot_ticks` | 188 | **893** |

`longest_run` is 9 in both: the kernel asks for nine sectors, is given nine,
and asks again. **QEMU missed this because SeaBIOS is a different BIOS**, not
because emulation cannot see it. So the boundary runs between the ROM and
the chip:

- **BIOS-level** — what `int 13h` returns, `int 1Eh`'s EOT, the ROM's own
  arithmetic: **reproduced**, because it is IBM's code executing.
- **Controller-level** — what a real NEC 765 puts in ST1 on a CRC error,
  whether a real drive ever returns short, what the result phase holds after
  an odd request: still the emulator author's belief, still the 5150's.
- **timing**: worth asking here, still checked on the 5150 before anything
  goes in PERFORMANCE.md Part 9's disk rows.

Not modelled: **motor spin-up** (the BIOS's own ~1 s wait is a CPU-timed loop
and so was always accurate, but the drive itself comes up instantly), **the
PIO paths** (PCjr), and **Format Track**, charged a flat revolution and never
calibrated. **Hard disks are untouched**: this is the floppy alone.

Everything on the CPU side agrees with the 5150 to within 0–4% across 45 of
47 `gfxbench` rows.

### Counting the traffic from outside: `m.disk()`

The counters are the **controller's**, read over the debug socket, so the
guest needs no `DISKCNT=1` kernel and no test package — a *shipped* image is
what you want to measure.

```python
m.disk(reset=True)          # ...drive the thing you care about...
print(m.disk())
# {'reads': 17, 'read_sectors': 186, 'longest_run': 18, 'writes': 0,
#  'write_sectors': 0, 'seeks': 24, 'seek_cylinders': 239, 'resets': 5,
#  'transfer_ms': 3236.7, 'seek_ms': 1912.0, 'ok': True}
```

What to read in it: **`longest_run` near the track length** is a kernel
batching properly; **`read_sectors` far above the payload** is §18.91's
shape; **`resets`** is a BIOS giving up, which is how GLaBIOS's 250 ms limit
was found.

### Where a whole BOOT goes: `tools/os88boot.py`

`m.disk()` says what the drive was asked for; this says what the *boot*
spent, phase by phase, and needs no knob kernel either.

```
python3 tools/os88boot.py --apps build/apps360.img --json build/boot.json
python3 tools/os88boot.py --build build/smallk --define KERN_SMALL --image build/small360.img
```

It is a **stopwatch and not a sampler** (`tools/os88prof.py` is the sampler,
for a package that loops). A boot is a straight line of named phases that
each run once, so the instrument is a breakpoint on the RETURN ADDRESS of
every `call` in `kmain` — the address a phase reaches exactly once, where a
breakpoint on the callee would fire for every other caller of `gfx_lock` too
— with the cycle counter and the FDC counters read at each. The addresses
come out of the kernel's own listing, asserted byte-identical to
`build/kernel-full.bin`, and the marks are armed one at a time so a mark
never reached is a timeout naming itself. The ROM's `int 13h` and the loading
bar are bracketed inside the boot sector by taking their return address off
the **guest's own stack** at the entry, because the sector relocates itself
to the top of whatever `int 12h` reported.

**Cycles are the answer and the host does not enter into it**: `cycles /
4772727` is seconds on the field machine, the drive's mechanics included.
Two full runs agree on every row to the cycle, and a complete walk agrees
with SPEC.md §15.4's boot timer to under one tick.

**On IRON, the knob instead.** `make BOOTPROF=1` (SPEC.md §15.5) asks the
same question from inside and draws the answer on the desktop. The two
instruments agreed row for row on `os8088_5150_cga` — `first paint` 178 ms
against 178.2, `mouse_init` 1200 against 1196.7.

**The machine still decides whether the number may be quoted**: a GLaBIOS
twin boots faster than any 5150 ever did, and only the IBM-ROM machines
answer for the field machine. The mechanical column does not move with the
ROM.

### GLaBIOS gives up on a floppy op after ~250 ms

That BIOS abandons a floppy operation after ~250 ms and resets the
controller, three times in a row, after which the boot sector prints `DSK`
status **80** — a timeout. It surfaced when the IBM machines were briefly
given 2:1 media, where a 9-sector run takes 372 ms; at 1:1 nothing here
reaches the limit. Three things said it was the BIOS and not the model: the
FDC presents a correctly BUSY status register for the whole delay, **seeks of
329 ms complete fine on the same machine in the same boot**, and the IBM ROM
completed the identical reads. It is one reason a disk number is not taken
off a GLaBIOS machine; the other is in *Which of them a DISK number may come
off* below.

### `int 19h` does not restart the machine on a GLaBIOS twin

The Chip menu's Restart (SPEC.md §20.10) ends in `int 19h`, and on
`os8088_5150_cga_gla` that leaves a **blank 80-column text screen with the
tick still running** and never boots. On `os8088_5150_cga` the same script
reboots properly: the card comes back to `Mode6HiResGraphics` about twenty
guest seconds later. So **a restart is tested on an IBM-ROM machine**, and a
liveness check on `0040:006C` alone will call the stalled one healthy — gate
on the video MODE coming back to graphics before believing anything after a
Restart.

---

## …but the BYTES it writes can be checked

MartyPC mounts a floppy by reading the file once into an in-memory
`DiskImage`; every sector the guest writes lands there and nowhere else.
Nothing in `martypc_headless` writes one back — that is the eframe
frontend's **Media ▸ Save Floppy As**, which a headless run does not have. So
a scripted session could drive os8088 into saving a document and then only
ask os8088 whether it worked, and **the writer and the reader are the same
FAT12 code**: both halves agreeing on the same wrong thing is precisely what
cannot be seen from inside (docs/FIELD-NOTES.md 4 is what that costs).

`flush` is the missing menu item, reached from the socket, and
**`tools/os88flush.py`** is what to do with the bytes on this side:

```
python3 tools/os88flush.py 127.0.0.1:9001 disks
python3 tools/os88flush.py 127.0.0.1:9001 save 0 /tmp/after.img
python3 tools/os88flush.py 127.0.0.1:9001 ls 1 APPS      # -R for the whole tree
python3 tools/os88flush.py 127.0.0.1:9001 get 0 SYSTEM.CFG /tmp/cfg.bin
python3 tools/os88flush.py 127.0.0.1:9001 diff 0
python3 tools/os88flush.py 127.0.0.1:9001 verify 0
```

…and, in a scripted session, sharing the one connection the debug server
allows:

```python
with os88marty.launch("build/os8088-360.img", apps="build/apps360.img") as m:
    f = os88flush.Flush(marty=m)
    assert not f.dirty(0)                     # nothing written at boot
    ...drive the UI...
    print(f.diff(0)["added"])                 # ['SYSTEM.CFG']
    cfg = f.volume(0).read("SYSTEM.CFG")      # its exact bytes, on the host
```

**`diff`, `dirty` and a bare `save` need the instance's run directory**,
because the drive reports its image as a path relative to the emulator's
working directory (`media/floppies/run0.img`). `Flush(marty=m)` takes it off
a Marty that `launch()` returned; a `Marty(addr)` opened by hand, or the
CLI, resolves against `build/martypc/run` and fails with *No such file* for
an instance under `build/martypc/inst/` — pass `run_dir=` / `--run-dir
build/martypc/inst/<tag>`, the directory of the log path `launch` printed.
`ls`, `get`, `verify` and `save <path>` need no reference and work either
way.

The `Volume` class walks the BPB, the FAT and the directories itself, with no
kernel code anywhere near it. It sees the **hidden and system** files SPEC.md
§19.6 marks and `disk_mount`'s species filter drops — a listing off a flushed
system disk shows `KERNEL.SYS`, `SOUND.DRV`, `HDD.DRV` and `ASSOC.DAT`, none
of them visible from inside os8088 — and `verify` hands the image to
`tools/os88disk.py --verify`, the same structural fsck the build uses.

Four things about it are load-bearing:

- **It pauses the machine, and it has to.** A save here is a multi-sector
  commit — data, then the FAT, then the directory entry (SPEC.md §18.4) —
  and caught mid-commit the volume that comes out is *genuinely*
  inconsistent. Every verb pauses, flushes and puts the machine back the way
  it found it.
- **`writes` is not a dirty flag, and it looks exactly like one.** The count
  in `disks` is fluxfox's `write_ct`; `post_load_process` sets it to **1** at
  mount and for a raw sector image it is never advanced (the one call that
  would is commented out upstream). It read 1 through a Control Panel close
  that demonstrably wrote three sectors. `dirty()` compares **content**
  against the image the drive was mounted from.
- **A bare `save(drive)` writes back over the mounted image** — the menu's
  *Save Floppy*, not *Save Floppy As* — which under `launch()` is the
  session's private copy. That destroys the only pristine copy `diff` and
  `dirty` compare against; name a path to keep the reference.
- **The emulator writes the file, so the path is the emulator's.** Its
  working directory is the run tree, so a relative path lands there rather
  than beside you; every verb hands the server an absolute path and reads
  the result back itself.

Verified: a fresh boot is `dirty() == False`; a Control Panel change plus a
close puts `SYSTEM.CFG` in `added`, with sectors 1, 3, 5 and 268 differing —
the two FAT copies, the root directory and the file's data cluster, SPEC.md
§18.4's commit order seen from outside. `get 1 APPS/HELLO.O88` off the live
disk is byte-identical to `build/hello.o88`.

---

## The machines

### Which of them a DISK number may come off

**The drive is the same everywhere and that is measured** (PERFORMANCE.md
Part 9 Set 38): one `combo.img` on every machine, with `m.disk()` read from
outside, puts the IBM-ROM ones bit-identical — 24 reads, 186 sectors, longest
run 9, 29 seeks, 54 cylinders. There is no per-machine drive constant.

**The BIOS is not the same, and it is what the number is made of.**

| class | machines | a disk TIMING here is… |
|---|---|---|
| **IBM ROM** (`rom_set = "ibm5150_82_v4"`) | `os8088_5150_cga`, `_herc`, `_both`, `_sb`, `_sbonly`, `_sb_128k`, `_sb_256k`, `_cga_hdd`, `_cga_720b`, `_cga_4fdd`, `_cga_ext720` | **field-comparable.** `sysbench`'s raw block lands 9–10 of 11 rows within one measurement quantum of docs/FIELD-MACHINES.md's 5150, 6–8 of them exactly |
| **GLaBIOS** (`glabios_pc` on a 5150, `glabios_xt` on a 5160) | every `_gla` machine, every `os8088_xt_*`, `_cga_128k`, `_cga_1fd`, `_cga_lpt`, `_gla_192k`, `_gla_128k` | **counts yes, seconds no.** A track read is **1.61x** lighter, and nine one-sector reads cost *the same as one* track read where the IBM ROM pays ten revolutions |
| **field clone ROMs** | `os8088_compaq_revh`, `os8088_eagle_spirit`, `os8088_columbia_mpc` | SPEC.md §18.93.2's question only |

The 1.61x is not an emulator artifact: on 1:1 media sector *n+1* follows *n*
immediately, so nine separate reads fit one revolution **if the BIOS turns a
call around inside one sector time (22 ms)**. GLaBIOS does; the 1982 ROM
cannot, its head-settle loop alone being 52.5 ms.

**No single machine is "the calibration".** Which rows land exactly shuffles
between the IBM machines — `_cga` nails both track rows and misses `seek 5
cyl`, `_herc` the reverse — because a row on a 13,731 µs quantum boundary
falls whichever side the guest's turnaround puts it. Quote the **class**, not
the machine.

### The list

`tools/martypc/configs/os8088_machines.toml` is appended to MartyPC's own
`ibm5150.toml` by `build.sh`; each stanza's own comment is the fuller
account. Every machine is 640 KB unless the row says otherwise. Which ROM a
machine runs is its `rom_set` line: `ibm5150_82_v4` is the period ROM (not in
this tree), `glabios_pc`/`glabios_xt` are bundled with MartyPC.

| config | what it is |
|---|---|
| `os8088_5150_cga` | the default: IBM 5150, 8088 at 4.77 MHz, CGA, real 1982 IBM BIOS |
| `os8088_5150_cga_gla` | the same with GLaBIOS — the ROM itself A/B'd with nothing else changed, and the machine most rows in the tree run on |
| `os8088_5150_herc` / `_herc_gla` | a Hercules. MartyPC models it as an MDA **subtype**, so the block needs `subtype = "Hercules"` as well as `type = "MDA"` (below) |
| `os8088_5150_herc_gla_144` | ...with **1.44 MB drives**, for `make combo144`. An anachronism on purpose — no stock XT reads 500 kbps media — so what it proves is that **our** boot sector and FAT12 code handle 18 spt on an 8088. Take no PERFORMANCE.md number off it |
| `os8088_5150_both` / `_both_gla` | **two cards**: a CGA *and* a Hercules, docs/FIELD-MACHINES.md's machine as it actually is (SPEC.md §39.11; `tests/dualcheck.py` runs `_both_gla` by default) |
| `os8088_5150_both_gla_mono` | `_both_gla` with `video_dip = "mda"` (patch 03): the switches set to mono, so os8088 boots on the Hercules — the calibration machine's arrangement, and the only machine that reaches SPEC.md §39.11.1's `vid_cga_alias` |
| `os8088_xt_vga` | an IBM 5160 XT with GLaBIOS and a VGA — SPEC.md §39's mode 12h. An XT because an 8-bit ISA VGA in a 5160 is a machine people built; a correctness instrument, never a timing one (the field has no VGA) |
| `os8088_xt_vga_sb` | ...with the AdLib + Sound Blaster pair, which exists **to be run with `--turbo`** (below) |
| `os8088_xt_vga_mda` / `os8088_xt_vga_herc` | a VGA beside an MDA / a Hercules — the other period two-card pair (docs/plans/completed/DUAL-DISPLAY-VGA.md). The apertures are disjoint (A0000+B8000 against B0000) so no patch is needed and block order decides nothing. The VGA is declared first, so it is MartyPC's card 0 |
| `os8088_5150_sb` / `_sb_gla` | an AdLib **and** a Sound Blaster (DSP 2.01, 0x220, IRQ 7). `_sb_gla` is the one a container can run; not a disk instrument |
| `os8088_5150_sbonly` | a DSP at 0x220 and **nothing at 0x388** — SPEC.md §51.3.1's jumpered-off-FM case; `_sb`/`_sbonly` are one pair with `make SNDSNIFF=sb` between them |
| `os8088_5150_sb_128k` / `_sb_256k` | `_sb` at 128 KB and 256 KB: SPEC.md §34.6's two claims are sized against the small one; Tracker cannot run at 128 KB at all, so the 256 KB one decides whether a MOD player's grants fit. IBM ROM, so neither boots in a container |
| `os8088_5150_cga_128k` | **the 128 KB floor machine** — `kern_small`'s own (`make small && python3 tests/small128.py`). GLaBIOS |
| `os8088_5150_cga_gla_256k` | 256 KB on GLaBIOS: the Weave family's floor (WEAVE-SPEC §1.4, `tests/weaveone.py`) |
| `os8088_5150_gla_192k` / `_gla_128k` | GLaBIOS at 192 KB and 128 KB, for SPEC.md §18.95.5's directory-cache width (192 KB picks 7 of 14 slots). `type` is the MOTHERBOARD and not the total — **check `[mem_top]` in the guest rather than the number in the file**: a first cut asked for 144 KB and came up with 192. `_gla_128k` never reaches a desktop with the shipped kernel, and that is the floor: `kern_big`'s `MIN_RAM_KB` is 196 and `kern_small`'s is 128 (docs/KERNEL-MEMORY.md), so 128 KB is `os8088_5150_cga_128k` with `make small` |
| `os8088_xt_hdd` | the XT with an **XT-IDE** controller — SPEC.md §52's rung 0 — **and a parallel port**, so it is the one machine where TWO drivers publish a Control Panel page at once (SPEC.md §31.9/§62.7) |
| `os8088_xt_hdd_sb` | ...and a Sound Blaster too: five volumes and two drivers at once, which is what the per-volume FAT windows (SPEC.md §18.8.1) can be counted on |
| `os8088_xt_vga_hdd` | the XT-IDE disk behind a **VGA**, the only place the hard-disk boot's VGA-only loading chrome (SPEC.md §15.3, §2.9.9.1) can be looked at. GLaBIOS's boot menu times out to A: before frame 300 here — the VGA BIOS's own init pushes POST out — so press `KeyC` more than once |
| `os8088_5150_cga_hdd` | the XT-IDE disk on a **5150 behind the period ROM** — the only crossing of "a hard disk" with "the ROM the reporter runs" (SPEC.md §18.91 is a defect only that ROM exposes). Needs the ROM |
| `os8088_5150_cga_lpt` | a Centronics card at 0x378 — SPEC.md §62's machine. MartyPC's `ParallelPort` stores what the guest writes to the status register, so the debug server's `outb` can be the far end: `tests/lptlink/partner.py` has driven a handshake, a mount and a recursive folder copy on it (§62.10.3, §62.10.6). Two traps: `net_connect`'s reply deadline is ten GUEST seconds and the guest free-runs at ~4x, so a 6 s host settle is 24 guest seconds of nobody answering and `net_lost` fires with **no wire traffic at all** — click with a short settle and answer immediately; and `Partner.serve` **steps** the guest, so call `m.run()` afterwards |
| `os8088_5150_cga_720b` | an **80-cylinder drive as B** — SPEC.md §18.96.2's machine, the only one where §22.12's Space key has something to toggle to. Declares `[machine.fdc]` itself. Put a non-FAT image of the size under test in B: the reach test is whether LBA 1439 can be written and read back |
| `os8088_5150_cga_4fdd` | four drives — two internal and the pair on the 37-pin external connector (SPEC.md §18.97). The 1982 ROM copies SW1 into `0040:0010`, so this machine must report 4 where `_cga` reports 2 |
| `os8088_5150_cga_ext720` | the field 5150's own drives plus an IBM 4865: two 360K, an 80-cylinder 720K as unit 2 and a 360K as unit 3 — SPEC.md §18.98's configuration, one machine carrying both of §18.96.2's answers |
| `os8088_5150_cga_1fd` | **one** drive, which is what the field 5150 has — SPEC.md §18.97's regression half (an uncontested machine must be untouched) |
| `os8088_compaq_revh`, `os8088_eagle_spirit`, `os8088_columbia_mpc` | XT-class **clone** BIOSes (`configs/os8088_field_roms.toml`), for SPEC.md §18.93.2: does this machine's FDC do the multi-track flip a cylinder-bounded run depends on? Boot `build/rdiag360.img` on each and read the map |

**Two cards took two patches to make honest.** Upstream maps a
Hercules-subtype MDA at B0000 **and** B8000 unconditionally, a CGA maps
B8000, and `Bus::register_map` resolves the overlap by last writer wins — so
one card silently vanished into the other, and which one depended only on
block order. `patches/02-hercules-page1-decode.patch` narrows the Hercules to
page 0, which is what a real card with 3BFh bit 1 clear decodes and what
`vid_setmode` leaves it at (SPEC.md §39.6). The obvious test — write B0000,
write B8000, read both back — does not catch it, because they are 32 KB
apart inside one card's 64 KB; `tests/dualcheck.py` asks the *rasters*
instead, and is verified to fail with patch 02 reverted. Then SPEC.md
§39.1's `vid_detect` reads `int 11h` bits 5:4, which upstream *derives* from
the card list ("any CGA present → CGA"), so no two-card machine could boot
mono: `patches/03-video-dip-config.patch` adds an optional `video_dip`
(`"mda"` / `"cga_lores"` / `"cga_hires"` / `"expansion"`); absent, nothing
changes.

**A two-card config lists the card os8088 will drive FIRST.** MartyPC's
primary is the first `[[machine.video]]` block, and `fbuf` with no `card=`
reports the primary — with the CGA first on `_both_gla_mono` the boot gate
watches an unprogrammed card and times out on a machine that booted
(`fbuf` returns 3 bytes). The DIP decides which card the **guest** picks; the
order decides which one the **tooling** looks at.

### `--turbo`: the fastest machine here is 7.16 MHz, and it is a CONTROL

`--turbo` takes the XT clock from 4.77 MHz to 7.16, and that is the whole
range: every machine MartyPC offers is an 8088/8086. 1.5x answers whether a
cost is **CPU-bound**, which shows as a proportional change. **The CGA panics
under turbo** — its video clock is derived from the CPU clock and
`devices/cga/videocard.rs` asserts on the result — so the turbo machine is a
VGA one (`os8088_xt_vga_sb`); the MDA is untested for the same reason. **No
PERFORMANCE.md number may be taken off it.**

### `subtype = "Hercules"` is load-bearing and its absence is silent

Without it MartyPC builds a plain MDA whose `mem_mask` is 0x0FFF: the card
decodes **4 KB and mirrors it eight times** across the 32 KB aperture. The
kernel's own probe still reports Hercules (`[vid_w]`/`[vid_h]`/`[vid_stride]`
= 720/348/90, `[vid_mono]` = 1), so **everything on the guest side looks
right** while every write above 0x0FFF aliases on top of the first 4 KB. What
it looks like: `shot` returns a **sheared** picture with the desktop repeated
down the screen, and `shot --rendered` an **all-black** 720x350. The
one-command diagnosis: `dump 0xB0000 32768 -o x.bin` and test whether byte
*i* equals byte *i*+4096.

### The hard-disk machines test RUNG 0, and that is the point

SPEC.md §52.1's rung 1 reads the IDE task file directly and is gated on
`CPU_286`, because an 8088's `in ax, dx` is two 8-bit bus cycles at the same
port and loses the drive's high byte — so on the target machine an option
ROM answering `int 13h` is the only transport, and QEMU can only ever test
the other rung. The controller is **XT-IDE** because MartyPC's romdef matches
the IBM/Xebec BIOS by MD5 and that ROM is IBM's; `ide_xtl.bin` (XTIDE
Universal BIOS, GPL) ships in `media/roms/XUB/`. The disk is MartyPC's
bundled `media/hdds/default_xtide.vhd`: 615/4/26 = **63,960 sectors**, just
under SPEC.md §18.7's cap. **Copy it rather than mounting it in place** — a
format is a write, and `launch()` clones it for you:

```sh
cp build/martypc/run/media/hdds/default_xtide.vhd /tmp/scratch.vhd
MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --machine-config-name os8088_xt_hdd \
    --mount fd:0:media/floppies/os8088-360.img \
    --mount hd:0:/tmp/scratch.vhd &
```

Verified: `HDD.DRV`'s Control Panel page reports `BIOS0  615x 4x 26  31M`,
Mount puts an `HDD C` zone on the desktop, and opening it lists the shipped
DOS filesystem. Two headless bugs had to be fixed in patch 01 to get there,
both of which look like the driver failing: `insert_vhds()` only knew the
Xebec (`hdc_mut()`), so an XT-IDE machine logged "No Hard Disk Controller
present" while having one; and `--mount hd:0:/abs/path.vhd` was resolved
through the resource manager, which only scans `media/hdds/`.

---

## The protocol

Newline-delimited JSON over TCP, one reply per command. `tools/os88marty.py`
is the client — a CLI, a REPL (address and no verb) and an importable
`Marty` class.

| command | |
|---|---|
| `ping` | the emulator's pid — what `launch` checks against the one it spawned |
| `status` | exec state, cycles, instructions, CS:IP |
| `regs` / `setreg` | all sixteen-bit registers and flags |
| `read` / `write` | memory, by flat `addr` or by `seg`+`off` |
| `inb` / `outb` | I/O ports |
| `run` / `pause` / `step` / `reset` | execution |
| `bp` | breakpoints: `exec`, `execseg`, `mem`, `memseg`, `int`, `io` — **replaces the whole set** |
| `park` | point the CPU at `cs:ip` with the prefetch queue flushed |
| `screen` | the video card's text, in text modes |
| `video` | which card, `mode`/`text`, its raster geometry and display apertures |
| `cards` | **every** video card in config order: `idx`, `type`, `primary`, `mode`, `field_w/h`, `frames`. The answer to "did my two-card config produce two cards", which `video` cannot give |
| `fbuf` | the card's RENDERED framebuffer as rgb24 — the only route on VGA |
| `flicker` | one sample per DISPLAYED FRAME: `changed`, `transient`, `bbox`, `settled` |
| `pace` | per-frame changed counts over a long run; `ignore` excludes a rect |
| `advance` | run a bounded amount of GUEST time — `frames=` or `cycles=` |
| `disk` | the FDC's counters (`m.disk()`), with `reset` |
| `disks` / `flush` | what is in each drive and where it was mounted from / write a drive's live image to a host file |
| `snapshot` / `restore` | fork a holder process; wake it on a port, any number of times |
| `key` | a keypress by MartyKey name — `KeyA`, `Enter`, `ArrowRight` |
| `mouse` | one Microsoft packet: relative `dx`/`dy` and buttons. **To click a CONTROL use `tools/os88mouse.py`** |
| `history` / `callstack` | the CPU's own instruction history |
| `quit` | stop the emulator |

**Every capture takes an optional `card=` and reports which card answered.**
Absent means the primary; otherwise an index (`VideoCardId.idx`, the
`[[machine.video]]` position — **not** iteration order) or a type name, and
an ambiguous type name is refused. It applies to `video`, `screen`, `fbuf`,
`flicker`, `pace` and `advance` — the last because `frames=` is a question
about one card and the two disagree, 50 Hz Hercules against 60 Hz CGA.

Load-bearing:

- **Reads do not perturb the machine.** Memory comes back through
  `BusInterface::get_vec_at_ex`, which costs no cycles and only ever *peeks*
  a mapped device — and **not** `peek_range`, which slices the flat memory
  vector and does not resolve MMIO, so a read of `0xB8000` returned whatever
  was in RAM under the card: a machine that had POSTed and printed `Disk Boot
  Fail` looked, through `read`, exactly like one that had hung. **I/O ports
  are the exception**: there is no peek for a port, so an `inb` is a real bus
  read, and several devices clear a status or advance a sequencer by being
  read at all.
- **A HIT IS NOT `"paused"` — it is `"breakpoint"`.** A wait written as
  `while status()["state"] != "paused"` is false forever at a breakpoint
  that is firing perfectly. Test `!= "running"` (what `until()` does, and
  what `Marty.stopped()` is), or `== "breakpoint"`. Five separate
  investigations here concluded "breakpoints do not fire in this build" and
  every one was the poll.
- **`sym()` is FLAT; `execseg`'s `off` is an OFFSET.** `sym("wm_show")`
  answers `KERNEL_SEG*16 + offset`, so it pairs with `{"type": "exec",
  "addr": ...}`. Put it in an `execseg`'s `off` and the breakpoint is armed
  0x600 further on, in real code that is never reached — no error, no hit.
  `Marty.bp_exec("wm_show", ...)` takes symbols or flat addresses and cannot
  get this wrong. `int` and `io` take their number in `addr` too:
  `{"type": "int", "addr": 0x13}`.
- **`execseg` and `memseg` are folded to flat addresses, because the
  segmented types do not work.** `BreakPointType::Execute(seg, off)` and
  `MemAccess(seg, off)` are declared in `breakpoints.rs` and matched by
  **neither** CPU; passed through, they arm silently and never fire
  (measured on `0060:37F5`, the timer hook). A flat breakpoint aliases every
  `seg:off` pair reaching the same linear address, which on a real-mode 8086
  is nearly always what was meant.
- **The `int` type catches `INT n` as well as hardware interrupts** —
  `sw_interrupt` ends in the same `intr_routine` — so `int` on 13h stops on
  the guest's own disk calls. An `int 08h` breakpoint fires every 262,144
  cycles (one tick) and `run` resumes it: MartyPC clears the latched
  breakpoint flag only in `machine.run()`'s `BreakpointHit → Run` arm, and a
  `run` that set the state itself skipped that clear and advanced **zero
  cycles, forever**. If a resume ever looks stuck, check `cycles` across two
  `status` calls before believing anything about the guest.
- **`reset` does not zero the cycle counter.** It is free-running for the
  life of the process, so every span is a delta.
- **`park` exists because `setreg ip` cannot be made to work.** `pc` is the
  FETCH pointer and `ip() = pc - queue.len()`, so a bare write leaves what
  the 8088 had prefetched from the old address in front of the new one, and
  those bytes execute first (parking at 0x0500 landed at 0xD4CC). The flush
  is not reachable through `CpuDispatch`, so `park` goes through the CPU's
  reset vector and **clears every register**; devices are untouched.

**DO NOT park the CPU to call a kernel routine when a flag the kernel already
polls will do it.** Forcing a `wm_paint_all` by building a stub, parking on
it and handing it a banked SS:SP works and is a trap with a long fuse: the
frame belongs to whichever task the pause caught, so if that was the UI task
inside a lock hold the stub's own `gfx_lock` yields, the scheduler switches
away, and the CS:IP restored at the end names a task whose stack has moved
on — it surfaced three assertions later as a window rect reading 2056x2056.
`tests/dispcalc.py`'s `full_repaint` is the shape to copy:

```python
m.cmd(cmd="run")
m.write(S("cp_dirty"), b"\x01")     # ui_task step 3: gfx_lock / wm_paint_all
while m.read(S("cp_dirty"), 1)[0]:  # / gfx_unlock, on its own stack
    time.sleep(0.05)
os88marty.settle(m); m.cmd(cmd="pause")
```

**Prefer poking a byte the guest already polls over executing kernel code
from outside** — `[cp_dirty]`, `[desk_zdirty]`, `[menu_bdirty]`,
`[cal_dirty]` and their kind are all deferred-work posts. The one cost is
that the repaint takes real wall time, so a run can straddle the menu bar's
once-a-minute clock change: exclude `y < MBAR_H, x >= [vid_clk_hx]`.

---

## What it is for, and what it is not

**For:** anything on an emulator. Breakpoints answer questions that used to
need a knob kernel: an `int` breakpoint on 13h counts disk calls on an
**unmodified shipped kernel**, where SPEC.md §18.94 needs `make DISKCNT=1`
and a test package.

**`verify`** dumps `KERNEL_SEG` and diffs it against `build/kernel.bin`, so
that live variables can be read at their listing offsets and the machine is
proved to be running the build you think. **It reads the file from offset
0, and since SPEC.md §2.9 put stage 2's blob at the front of the image
`.text` sits at file offset `BOOT2_PAD` (`BOOT2_SECS` x 512 = 4,096)** — so
against a correct kernel it reports ~96% differing today and its
`boot_ticks ... in the file` line reads a blob byte. Until it skips the blob
(and `.cold` at `COLD_SEG`, `.ovl` where `[spl_fseg]` says), the proof that
the guest runs your build is `m.sym()` — which asserts its own map against
`build/kernel.bin` — plus `m.read(m.sym("boot_ticks"), 2)` reading a
stamped tick rather than `0xFFFF`.

### Screenshots, without leaving

`os88marty.py <addr> shot out.png` reads the framebuffer straight out of
VRAM and decodes SPEC.md §39.3's banked layout — the same arithmetic
`tools/hercshot.py` applies to QEMU, so a picture from either route is the
same picture. **Do not start QEMU just to look at the screen.** The card is
asked which it is (`video`), never sniffed: an unmapped `0xB0000` reads as
zeroes rather than erroring, so "is there something at the MDA aperture"
answers yes on a CGA-only machine.

`shot` reads **guest VRAM** and is CGA and Hercules only, a property of the
format: both are 1bpp, so the bytes *are* the pixels. Mode 12h is four planes
behind the Graphics Controller's Read Map Select and is not readable as flat
memory.

**`shot --rendered` is the other route, and it covers everything.** It asks
the CARD what it rasterised (`fbuf`), so it works in every mode on every
adapter and comes back as 24-bit colour; VGA takes it automatically. The two
are a genuine cross-check: on a CGA desktop they agree on every pixel (76,218
lit of 128,000 by both routes). Reach for the VRAM route by default on the
1bpp adapters, because its output is byte-comparable with `hercshot.py` and
so with every "0 differing pixels" check in this tree.

Two traps live inside `fbuf`, and both produce a black or sheared picture
rather than an error. `display_buf()` casts the card's own array to `&[u8]`,
and the cards disagree about what an element is: CGA, MDA and EGA hold
one-byte palette indices, **VGA holds packed RGBA at four bytes per pixel**
— and a wrong guess still yields a plausible histogram. Deriving the size
from `buf.len() / (pitch * field_h)` is wrong twice over — the buffer is
allocated at the card's **maximum** raster, and `field_h` on a double-scanned
CGA is twice the rows rendered; `render_depth()` is the card's own answer.

### Input, without a guest module and without QEMU

`key` enters the emulator's keyboard buffer, so the guest sees it through the
8255 and int 09h; `mouse` builds a **real Microsoft 3-byte packet** and
clocks it into the serial controller, so the guest's own `mou_isr` decodes
it. Both exercise *more* than a poke would — a debug module writing
`[mouse_x]` would skip the UART, the packet decoder and SPEC.md §9.5's port
contest, which is the code most likely to be wrong — and it is better than
QEMU's `msmouse`, which is not a UART-level device and ignores DTR
(docs/TESTING.md). Verified without a screenshot: `mou_seen` (SPEC.md
§9.4.2, set only on a complete decoded packet) goes 0 → 1 under injected
packets; and on a machine whose mouse has not spoken the arrow keys *are*
the mouse (SPEC.md §9.6), so ten `ArrowRight` presses moved `mouse_x` from
320 to 350 — the whole path, buffer, 8255, int 09h, BIOS buffer, int 16h,
`kbm_poll`.

`Marty` wraps both: `key(name)`, `type_text(s)`, `ctrl(name)`, `mouse(dx, dy,
l, r)`; the CLI verbs are `key`, `type` and `mouse dx dy [--click]`. `key`
names a **MartyKey** variant — `KeyC`, `Enter`, `ArrowUp`, `Digit1` — and a
bare `'c'` is refused rather than guessed at.

**The speed scaler is forced to 1.0 by the `mouse` command, and that is what
makes counting in pixels work at all.** MartyPC's mouse defaults to
`DEFAULT_MOUSE_SPEED = 0.25` (`devices/mouse.rs`), so an unscaled `dx` of 60
reaches the guest as 15 and a script that counts from the kernel's edge
clamp lands **a quarter of the way** to everything it aims at, with every
click missing silently. There is no TOML key for it (`SerialMouseConfig`
carries only `type` and `port`), so the fix is at the command.

### Audio capture

Headless MartyPC had none — marty_core's `sound` feature was not enabled for
the crate. `MARTYPC_WAV=/tmp/cap` writes **one 16-bit PCM file per sound
source** at that source's own rate (`/tmp/cap.pc_speaker.wav`), no mixing,
in the format `tools/sndcheck.py` already parses, so every existing
assertion — RMS, the Goertzel dominant-frequency scan, `--expect-silence` —
works against it unchanged. Verified by programming PIT channel 2 for 880 Hz
through `outb` alone: `sndcheck` reported dominant 891.0 Hz.

Two differences from QEMU's `-audiodev wav`: the capture is **continuous**
(QEMU's pcspk stream only runs while the speaker is on, so its file time
*is* speaker-on time), and **the guest is also driving port 61h**, so a tone
you open by hand may be closed again by `snd_tick` a moment later.

### The Sound Blaster

`devices/sblaster.rs`, added by patch 01: upstream had `adlib.rs` (an OPL2
via `opl3_rs`) and `dma.rs` but no DSP, so SPEC.md §34.5's stream tier could
only be reached under QEMU. A DSP 2.01 by default at `0x220`/IRQ 7 on 8-bit
DMA channel 1:

```toml
    [[machine.sound]]
    type = "SoundBlaster"
    io_base = 0x220
    irq = 7
    dsp_version = [2, 1]
```

`dsp_version` is the knob that matters, because it is what the driver
branches on: at `[2, 1]` os8088 takes the classic `0x48` + `0x1C` auto-init
path; at `[1, 5]` the same driver has to re-arm the 8237 per half-buffer,
which is a code path in `sb.inc` nothing else can make it take. The
SB16-only commands (`0x41`, `0xC6`) are **refused rather than
half-implemented**.

It models the reset handshake, `0xE1` version, `0xF2` forced IRQ (how a
driver *finds* its line), `0x40` time constant, `0x48` block length,
`0x14`/`0x24` single-cycle and `0x1C`/`0x2C` auto-init in both directions,
`0xD0`/`0xD4` pause and continue, `0xD1`/`0xD3` speaker and `0xDA`
exit-at-the-block-boundary. Reading base+0xE acknowledges the 8-bit IRQ, and
a block completing while the previous interrupt is still unacknowledged is
counted as a **missed ack** rather than hidden. It pulls bytes through the
real 8237 (`do_dma_read_u8`, the FDC's call) and resamples through a carried
fractional accumulator, so a long stream does not drift (an auto-init loop
ran 53.96 s of guest time without leaving 1000.0 Hz). `tests/sbtest` is the
gate: 2.00 s at dominant 1000.0 Hz, and an underrun leg of exactly 2,400
granted bytes then silence.

**One caveat: the `0x10` direct-DAC command is accepted and dropped.**
Nothing in this tree uses it — os8088's driver is DMA-only — but a program
that does will hear nothing and get no error.

### VGA mode 12h

marty_core ships a register-level VGA whose `vga` feature is on by default,
and it rasterises 12h correctly. What patch 01 fixed was one line in the
headless crate's `Cargo.toml`: `marty_frontend_common` was taken with
`default-features = false`, so the arm of `get_rom_requirements` that asks
for `ibm_vga` was compiled out and the machine came up with a VGA and no
video BIOS, silently. The VGA BIOS is MartyPC's bundled `BOCHS-VGABIOS.bin`
(LGPL). Two symptoms send a diagnosis the wrong way: the card's
`is_in_graphics_mode()` answers **false in mode 12h** (`mode_graphics` is
never assigned in the VGA, so `video`'s `graphics` field is dead there — use
`mode`/`text`), and a framebuffer read as one byte per pixel comes back 57%
"index 255", which is RGBA read wrong. `field_w`/`field_h` is the honest
question: **800x524 is mode 12h's raster**.

`os8088_xt_vga` is the machine; verified end to end with `vid_w=640
vid_h=480 vid_planes=4 stride=80` and Minesweeper drawing eight distinct
palette colours.

## Capturing a screen — every mode, including the fullscreen ones

Three capture routes answer different questions. Picking the wrong one does
not error; it produces a plausible picture of nothing.

| route | what it is | works in |
|---|---|---|
| `shot` (VRAM) | decodes SPEC.md §39.3's banked **graphics** framebuffer out of guest memory | CGA mode 6, Hercules graphics |
| `shot --rendered` (`fbuf`) | asks the CARD what it rasterised, as rgb24 | **every mode**, every adapter |
| `screen` | the card's text rows as **characters** | text modes |

**THE RENDERED FRAME IS ONLY AS CURRENT AS THE RASTER.** Stop the machine at
a breakpoint half way down a frame and `fbuf` gives you the new picture above
the beam and the PREVIOUS one below it — and two captures of the same
stopped machine agree with each other perfectly, because both read the same
stale buffer. It looks like a primitive that drew twenty rows and stopped,
starting at a different row every run. The tell is the one thing a rendered
frame cannot fake: **read the PLANES**. They are memory and always current;
`tests/blitp.py` is the worked example — drive Read Map Select (GC4 = 4,
then the plane number to 3CF) through `outb` and read `0xA0000`. Use `fbuf`
for a settled machine and the planes for a stopped one.

**THE RENDERED FRAME IS NOT IN THE GUEST'S COORDINATE SYSTEM EITHER.** A
whole-screen capture does not care; a CROP does. On a Hercules the card's
frame is 720x350 for a 720x348 screen at dx = −16, dy = +2 (above); VGA and
CGA come back at (0, 0). There is no correction to apply blind — the offset
is the card's raster phase. What it looks like: a crop at a window's guest
rect samples 16 columns to the *right*, so the middle compares perfectly and
the rightmost columns show the window *behind* it — which reads as a smeared
restore, the exact defect `tools/sucheck.py` exists to detect, and it
survives a forced-full-repaint control because both are mis-cropped
identically. So: **crop with `vram` on the 1bpp adapters**, and on VGA at
least **assert `fbuf`'s dimensions against `[vid_w]`/`[vid_h]`** before
believing a crop. `tools/sucheck.py`'s `fb()` is the worked example.

**`video` reports `mode` and `text`, and that is the discriminator.** It
comes from the card's `display_mode()`, derived from its registers — unlike
`graphics`, which is dead on the VGA. `shot` reads it and routes itself:
SPEC.md §53.4's `FSXM_TEXT80` puts a fullscreen app into an 80x25 **text**
mode (Tracker's XT-mode fullscreen, §45.13), where the VRAM route would
decode character/attribute pairs as a bitmap and return a full-size image
of noise.

```
$ os88marty.py <addr> shot out.png
out.png: card is in Mode3TextCo80 (a TEXT mode) - capturing the RENDERED framebuffer.
  The VRAM route would decode character cells as a bitmap and show you nothing real.
  For the characters themselves: os88marty.py <addr> screen
```

`--kind cga` against a text mode is **refused** rather than obeyed. The nine
`FSXM_*` ids (`apps/os88api.inc`) reduce to three cases:

| `FSXM_*` | what it is | capture with |
|---|---|---|
| `TEXT80`, `TEXT40` | 80x25 / 40x25 text | **`screen`** for content (`"Pos 08/52" in rows[1]`), `shot` (auto-rendered) for pixels |
| `CGA320`, `CGA640`, `HERC` | 1bpp / 4-colour banked graphics | `shot` — VRAM route, byte-comparable with `hercshot.py` |
| `VGA0D`, `VGA13`, `VGA12`, `MODEX` | planar or chunky VGA | `shot` — auto-rendered |

**Flicker is measurable here, and that is PERFORMANCE.md Part 3.1.** The
`flicker` command steps the machine until the card finishes a frame, grabs
the rendered buffer, and repeats — sampling the glass exactly as often as an
eye does. It reports `changed` per frame (a **visible redraw**, priced in
frames × the frame period) and `transient` — pixels whose value before and
after the operation is the *same* but which showed something else in
between: the **double-draw flash** as arithmetic, needing no notion of
"background" and unable to fire on an honest change.

```sh
python3 tools/os88marty.py 127.0.0.1:9001 flicker -n 90 --click
```

A Disk window's full repaint measures **11 frames (183 ms) of redraw and
1,963 flashed pixels for 10 frames** on CGA; an idle desktop and a bare
pointer move measure zero. Three traps, all in Part 3.1: inject the input
while **paused** so the action lands inside the capture; check `settled` or
every count was measured against a moving target; and **always read the
`bbox`** — a count alone misattributes, which is how 42 pixels of "text
flash" turned out to be the mouse pointer blinking under the gfx lock.

**It works on Hercules**: a 12-frame capture of an idle Hercules desktop
returns `settled` with 0 changed and 0 transient at 720x350, frames every
~93,500 cycles (19.6 ms, ~51 Hz). An MDA sitting in a machine whose guest is
driving the *other* card reads 0 lit and never advances `frame_count()` —
that is a card nobody has programmed, not a card that cannot rasterise. Two
limits remain: `mode`/`text` is dead on the MDA (it answers `Mode0TextBw40`
in Hercules graphics), so `field_w`/`field_h` is the discriminator there, as
on VGA.

### Determinism and snapshots

**The emulator is bit-exact deterministic**: two independent processes reach
a breakpoint at the same cycle count with the same 1 MB memory hash, and stay
identical through injected input — so "continue from a known state" is
available by replaying the inputs (docs/plans/completed/SNAPSHOT-PLAN.md §7).
**A wall-clock client destroys that**: two free-running instances paused
after the same `sleep(22)` were 21.7 M cycles apart. So `advance(frames=…)` /
`advance(cycles=…)` is the way to wait, never `time.sleep`, and
`tools/os88mouserel.py` is the mouse driver paced that way.

`snapshot`/`restore` freezes a state outright by forking a holder process,
so nothing can be left out of it:

```python
s = m.snapshot()               # {'id': 1, 'cycles': 167309139}
r = m.restore(s["id"], 9995)   # a Marty on the restored machine, byte-identical
r.quit(); r = m.restore(s["id"], 9995)   # …and again, from the same state
```

Unix only, in-memory only, and the mounted floppy is shared rather than
rolled back — SNAPSHOT-PLAN §8 has the limits.

### Not for

The real 5150: `DEBUG.DRV` is gone (SPEC.md §58), so on iron the floor is
SPEC.md §57's registry read out of a photograph and a dump taken by whoever
has the machine. And not for a machine that is not an 8088: the 286 and 386
targets are 86Box's.

**A number from it is still a number from an emulator.**
docs/FIELD-MACHINES.md's first rule is unchanged: a timing goes in
PERFORMANCE.md Part 9 labelled MartyPC, and a dump is evidence about *logic*.
On the CPU that labelling is a formality — it agrees with the 5150 to 0–4%
— and `step` gives real cycle counts (50 instructions measured 719 cycles on
a booted desktop, 14.4 each). **The guest's cycle count is a DELTA
instrument**: `cycles` is the age of the process, not of the run, and the
rate against wall clock (~3.4–4.8x here) belongs to the HOST, so size every
`settle(limit=)` as though guest seconds arrive faster than wall ones,
because they do.

---

## What was verified, and how

Run end to end in a container with no IBM ROM, on `os8088_5150_cga_gla` with
`build/os8088-360.img` and `build/apps360.img`:

- `make marty` from a clean `build/` clones, patches and builds in a couple
  of minutes with a warm cargo registry.
- `os88marty.py launch` with the default machine exits at once naming the
  missing `ibm5150_82_v4`; with `--machine os8088_5150_cga_gla` it prints its
  address and outlives the command; `instances` lists it; `kill <port>` ends
  it; `reap` afterwards reports 0 orphans and leaves live owned instances
  alone.
- `status`, `regs` (`cs=0060 ds=0060`, SS = `LOW_SEG` — SPEC.md §1's near
  model), `settle` (2.1 s host on a settled desktop), `m.sym()`, `m.disk()`,
  `advance(frames=5)` (380,039 cycles), `dump ... -o`, `os88mouse.py where`.
- A second `Marty(addr)` is refused in under a second naming the holder.
- An `int 8` breakpoint stops the guest in state `"breakpoint"`
  (`stopped()` True), `run` resumes it, and the next hit is 262,144 cycles
  later.
- `shot` and `shot --rendered` agree pixel for pixel (76,218 lit of 128,000).
- `os88flush.py disks`, `ls 1 APPS`, `verify 0` (`os88disk: verify OK`),
  `save 0 <path>` (368,640 bytes), `get 1 APPS/HELLO.O88` byte-identical to
  `build/hello.o88`; `diff 0` reports 0 sectors once `--run-dir` names the
  instance's directory.
- `verify` reports ~96% differing against a correct kernel, for the reason
  given under *What it is for*.

The IBM-ROM claims — the BIOS date at `0xFFFF5` reading `10/27/82`, the
disk calibration tables, `int 19h` restarting — were taken on a checkout
carrying the ROM and are not re-checkable without it.

---

## Upstream findings

All are in `tools/martypc/patches/01-headless-debug-server.patch` and worth
offering upstream:

- **`peek_range` was off by one.** `if address + len < self.memory.len()`
  refuses a range *ending* at the last byte of memory — `peek_range(0xFFFF0,
  16)`, the reset vector paragraph. `<=`. (No longer load-bearing here;
  `read` uses `get_vec_at_ex`.)
- **Two breakpoint types are dead code.** `BreakPointType::Execute(seg,
  off)` and `MemAccess(seg, off)` are in the public enum and no CPU matches
  on them. A control that looks live and is not is the sharpest kind of bug
  in a debugger, because it makes the *absence* of a stop look like
  evidence.
- **Headless mode never mounted floppies.** `--mount fd:N:path` is parsed
  into `config.emulator.media.floppy` and then nothing reads it — mounting is
  the eframe frontend's file manager's job. A headless machine always booted
  with empty drives, which GLaBIOS reports as `Disk Boot Fail. You monster.`
  and the IBM BIOS by dropping into cassette BASIC.
- **A bitstream track's write counter never advances** (fluxfox, branch
  `marty_consumer_0.34`). `DiskImage::write_sector` delegates to the track
  and increments nothing; `BitStreamTrack::add_write`'s only call site is
  inside a commented-out block. Since `post_load_process` sets the count to
  1 at mount, `write_ct` on a raw sector image reads 1 for the life of the
  machine. `tools/os88flush.py` compares content instead; the fix upstream
  is one uncommented line.
- **`attach_image` takes a path and throws it away** (the parameter is
  `_path`), so once an image is in a drive nothing knows where its bytes came
  from. `mount_floppy` keeps its own registry so that `flush` can write back
  over the file the drive was mounted from.
- **`insert_vhds()` knew only the Xebec** and resolved a VHD through the
  resource manager, so an XT-IDE machine had no disk and an absolute path
  was "not found" (above).
- **`run` from a breakpoint advanced zero cycles** unless the transition went
  through `machine.run()`'s `BreakpointHit` arm (above).

The server itself is the answer to the crate's own standing TODO — *"We
don't have any backend to run an event loop. If we want to actually run the
emulator now we need some way of controlling / stopping it."* A socket is
both.
