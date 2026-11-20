# Boratype native target driver

`tests/boratype_target.py` is an installed-package native driver. It opens
the software disk through `os88ui.open_drive`, `path` and `open`, delivers real
keyboard/mouse events, and reads the production session/profile. It does not
write application state, patch the guest, advance frames or wait on host time.
MartyPC is the default. Actual EGA uses automation-capable86Box; VESA uses
QEMU's VBE and the shipped VESA driver. The live adapter, dimensions and depth
are asserted after boot. Initial adapter diagnoses have run; final repaired-head
smoke, reboot, windows and challenge evidence remains integration work.

Build the complete application and its source disk first:

```
make boratypedisk
python3 tests/boratype_target.py --host-check
python3 tests/boratype_target.py --scenario smoke --machine os8088_xt_vga --evidence build/boratype-qa-vga
python3 tests/boratype_target.py --scenario challenges --evidence build/boratype-qa-deadlines
python3 tests/boratype_target.py --scenario adapters --adapter cga --evidence build/boratype-qa-cga
python3 tests/boratype_target.py --scenario adapters --adapter ega --backend 86box --evidence build/boratype-qa-ega
python3 tests/boratype_target.py --scenario adapters --adapter vesa800 --evidence build/boratype-qa-vesa800
python3 tests/boratype_target.py --scenario adapters --adapter vesa1024 --evidence build/boratype-qa-vesa1024
python3 tests/boratype_target.py --scenario windows --evidence build/boratype-qa-windows
```

The default images are `os8088-360.img` and `boratype360.img` beside the exact
`kernel.bin` that `os88sym` selected. `--image` and `--apps` select other
prepared disk images; `--path` selects the installed guest package path
(default `B:/BORATYPE.O88`). `--machine` is the MartyPC machine name. Use a
machine whose drive geometry and adapter match those images. Native fullscreen
in this driver follows the OS adapter. CGA's adapter arm also enters its
320x200 four-color FSX bracket, types a complete lesson there and restores
native640x200 mono. It enters FSX from Home before starting a lesson, so a
native play-renderer defect cannot conceal an independent FSX result. That
does not pass mono lesson input: run the full smoke arm on CGA separately.
`--backend` selects the instrument explicitly. QEMU is refused for actual
CGA/Hercules/EGA assertions. EGA defaults to an actual640KB8088/EGA machine
on86Box. VESA defaults to QEMU, with no XT timing claim. Missing drivers or
emulators fail preflight rather than silently substituting a VGA pass.

For VESA, existing `vesa8.img` / `vesa.img` gate disks are accepted. If absent,
the driver copies shipped `os8088.img` into its evidence directory and uses
the existing FAT host tool to add the canonical real `SYSTEM.CFG`: `DW` bit9
wants `VESA.DRV`; `VM` version3 class0x8A requests800x600x8, while Auto selects
1024x768x16 on QEMU. The shipped driver must be on that private disk. Source
disks/settings are never overwritten. Actual CFG records and attached mode
are recorded in the report.

`OS88_SRC`, `OS88_BUILD` and the run tree are respected. The app's NASM map is
rebuilt from `SRC` with the selected build's generated C assembly and asserted
byte-identical to the complete `boratype.bin`, including its appended overlay.
`os88sym` independently asserts the kernel source matches its built binary;
the boot disk's `KERNEL.SYS` must match that build's shipped `kernel.sys`
(the compressed SYS and raw mapped BIN are different formats). The default
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
  generation/profile intact.
- Closed-session in-RAM floppy writes export through `Flush`. The host's
  independent FAT reader checks the actual save copies. Both exported disks
  cold-boot a new machine with bootcache off; a fresh package must load the
  exact whole profile, preference and newest generation, with no obsolete
  writer ownership. Exported images remain evidence for inspection.

Ordinary completion sends one printable character at a time and waits for
its production A/K/E/N witness. Queuing a whole drill at once is BIOS-ring
stress and can lose characters while a handler paints; that is a different
experiment. Stress needs queue instrumentation before attributing lost input
to the app.

The `windows` arm (also in smoke) covers the app with the actual Disk window,
proves overlapping rectangles and focus loss, checks frozen progress/time,
raises it and requires intentional Resume. It drags the live grow box below
the package's published minimum, verifies clamped content and essential
Resume, types at that size, and grows back. Grow geometry comes from the
current kernel's `wm_grow_rect`; tracked outline/origin fields prove the
gesture. No kernel variable is cleared to manufacture that proof. Other OS
gestures use `os88ui` directly.

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
best-effort raw current-guest screenshot before exiting nonzero. A failed
capture bypasses a potentially broken idle gate and is marked `settled:false`;
it is diagnostic evidence. FSX waits on actual VRAM stability instead of
`ui_done`, since the bracket owns the UI callback. The report also includes image
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
positive control. The final repaired-head smoke/red arm remains pending. Root owns
suite registration, the measured runtime, final affected soak union and the
single `test-full` pass.


The private Mac86Box instrument can be built without changing emulator source:
use the pinned source/ROM commits and all patches from `tools/86box/`, install
CMake/Ninja/Qt5/libserialport/FAudio/Vulkan headers through Homebrew, then use
the build script's CMake options plus `CMAKE_MACOSX_BUNDLE=OFF`, explicit
`Qt5_ROOT` and `Qt5LinguistTools_ROOT`, and `LIBRASHADER=OFF`. Build with Ninja,
copy `src/build/src/86Box` into the instrument tree and write the normal patch
fingerprint only after the build succeeds. `OS88_86BOX` and
`OS88_86BOX_ROMS` can point the runner at it from another worktree. The
installed GUI86Box and the automation instrument are distinct binaries;
a GUI installation alone does not prove the socket is available.
