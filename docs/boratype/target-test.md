# Boratype native target driver

`tests/boratype_target.py` is an installed-package MartyPC driver. It opens
the software disk through `os88ui.open_drive`, `path` and `open`, delivers real
keyboard/mouse events, and reads the production session/profile. It does not
write application state, patch the guest, advance frames or wait on host time.
The driver has been Python-compiled and its offset helper has run on the host.
**It has not been run in a guest by its author.** Its actual native results,
runtime and screenshots remain integration work.

Build the complete application and its source disk first:

```
make boratypedisk
python3 tests/boratype_target.py --host-check
python3 tests/boratype_target.py --scenario smoke --machine os8088_xt_vga --evidence build/boratype-qa-vga
python3 tests/boratype_target.py --scenario challenges --evidence build/boratype-qa-deadlines
```

The default images are `os8088-360.img` and `boratype360.img` beside the exact
`kernel.bin` that `os88sym` selected. `--image` and `--apps` select other
prepared disk images; `--path` selects the installed guest package path
(default `B:/BORATYPE.O88`). `--machine` is the MartyPC machine name. Use a
machine whose drive geometry and adapter match those images. Native fullscreen
in this driver follows the OS adapter; CGA four-color FSX needs its separate
root driver. This driver does not claim EGA/VESA or PCM capability merely
because a machine or picture has that name.

`OS88_SRC`, `OS88_BUILD` and the run tree are respected. The app's NASM map is
rebuilt from `SRC` with the selected build's generated C assembly and asserted
byte-identical to the complete `boratype.bin`, including its appended overlay.
`os88sym` independently asserts the kernel source matches its built binary;
the boot disk's `KERNEL.SYS` must match that same `kernel.bin`. The default
software disk's `BORATYPE.O88` must match the built package. After launch,
native header sizes and hot entry instructions are compared to the mapped
binary as a further stale-install check. All runtime reads use `M.Pkg` so
compaction cannot leave a banked package segment behind.

The driver compiles a tiny host C helper against the actual production
`bt.h`, with `BT_HOST` selecting `uint16_t`. It derives every `BTSession`,
`BTProfile` and `BTStore` field offset and size using `offsetof`/`sizeof`.
These structures contain no pointers or native `int`; their byte/word
alignment is checked at no more than two bytes. Native UI/audio globals are
read as two-byte ints. No serialized-file offset is reused as an in-memory
profile offset: the target profile has alignment padding the file lacks.

The smoke scenario asserts:

- Home and lesson1 work from a real installed package; lesson selection uses
  the production face's action rectangles, read from its guest hit cache.
- One wrong printable key leaves the target and records one error. Correct,
  Backspace and retype preserve historical A/K/E while retracting/restoring N.
  The rest of the drill is read from guest memory and typed through the OS.
- The finished result matches independent A/K/E/N, rounded accuracy, exact PIT
  seconds, WPM and best streak. It commits, records mastery and achievements.
- An authored mixed sentence is typed with real Shift/case/punctuation and
  yields exact counters and 100-percent accuracy.
- Retry resets the lesson. Timing starts on a real accepted key before a
  pause, freezes while the kernel tick counter advances, and resumes.
- F6 enters native fullscreen and returns, preserving position and seed and
  restoring the actual saved window geometry and OS fullscreen ownership.
- Help, Settings and Records are reachable; a real music control toggles its
  saved preference. File/Save waits for a new validated generation.
- Close/reopen retains the entire profile and preference. A second live
  package instance is read-only, refuses a save, and leaves the first writer's
  generation/profile intact. This is not a reboot test.

The challenges scenario runs actual 60-, 180- and 300-second deadlines without
typing or modifying guest state. Countdown time is outside scoring; elapsed
ticks must be exact ceiling values1093/3278/5462, remaining zero, no attempts,
and no record replacement. It additionally compares a ranked one-key
one-minute test with the same category paused/resumed and unranked; the latter
must preserve its previous comparable record. This is about eleven guest
minutes, plus UI work. Its host runtime has not been measured; do not register
a guessed suite `secs` value.

`--scenario audio` probes a three-error reaction after the real cooldown,
and master mute/unmute while input remains usable. It explicitly makes no
audibility, PCM clip, mixing, FM or speaker capture/listening claim. Root's
audio QA provides that evidence separately.

Every screenshot comes from `ui.m.fbuf()` after the native UI finishes, and
each is paired with the actual session snapshot in `report.json`. Captures
are marked `inspection: required`; a path alone is not a passed appearance
claim. A failed assertion records its traceback, current guest state and a
best-effort screenshot before exiting nonzero. The report also includes image
hashes, guest cycles and a heap-consistent package/owned-claim memory ledger.
Individual input cycle readings include OS consumption and UI completion;
they are upper-bound observations for those gestures, not isolated renderer
benchmarks or a proven 100ms XT claim.

Prove a target assertion can go red after the integrated smoke run works:

```
python3 tests/boratype_target.py --scenario smoke --negative --evidence build/boratype-qa-red
```

The red arm delivers the same real wrong character, then deliberately expects
zero errors. It must fail after observing that input; it does not manufacture
its result by writing guest memory. Run the normal smoke arm afterward for a
positive control. The author has not executed either guest arm. Root owns
suite registration, the measured runtime, final affected soak union and the
single `test-full` pass.
