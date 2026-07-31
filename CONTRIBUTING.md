# Contributing to os8088

os8088 is ~14KB of hand-written real-mode 8086 assembly. That sounds
forbidding, and it is — but it is also exactly the kind of codebase a coding
agent is good at, because every rule it has to obey is written down in
[SPEC.md](SPEC.md) and [CLAUDE.md](CLAUDE.md), and every change can be
verified by booting the thing and looking at it.

This guide gets you from a clean machine to a verified pull request using
**Claude Code** or **OpenAI Codex**, on macOS, Linux or Windows. You do not
need to know 8086 assembly to start. You do need to boot what you changed
before you send it.

## The short version

```
git clone https://github.com/jggonz/os8088.git
cd os8088
git config core.hooksPath .githooks     # one-time: enables the secret-scan hook
make                                    # builds all four floppy images
make run                                # boots it in QEMU
claude          # ...or: codex
```

Then tell the agent what you want, and make it read `SPEC.md` first.

## 1. Toolchain

Three tools do everything: **nasm** (assembler), **qemu-system-i386**
(emulator), **python3** (the packaging and test tooling). Plus `git` and
`make`, which you probably have. There is no linker and no cross-compiler —
NASM emits flat binaries directly.

Optional: **gitleaks** (the pre-commit hook wants it), **gdb** (for
`make debug`), **86Box** (for `make xt`, the emulated IBM PC/XT).

### macOS

```
brew install nasm qemu python3
brew install gitleaks              # optional but recommended
brew install --cask 86box          # optional, only for `make xt`
```

Xcode Command Line Tools supply `make` and `git` (`xcode-select --install`).

Apple's linker only speaks Mach-O, which is part of why this project uses
`nasm -f bin` and no linker at all — so there is nothing else to install.

### Linux

Debian / Ubuntu:

```
sudo apt install nasm qemu-system-x86 python3 make git
```

Fedora:

```
sudo dnf install nasm qemu-system-x86 python3 make git
```

Arch:

```
sudo pacman -S nasm qemu-system-x86 python make git
```

gitleaks: your distro may package it (`apt install gitleaks`,
`pacman -S gitleaks`); otherwise grab a release binary from
[gitleaks/gitleaks](https://github.com/gitleaks/gitleaks) and put it on PATH.

`make run` opens a QEMU window, so you want a desktop session. Over SSH or in
a container, use `make test` (headless) and drive it over QMP — see §4.

### Windows

Use **WSL2** and follow the Linux instructions inside it. This is not a
stylistic preference:

- `tools/qmp.py` talks to QEMU over a **Unix domain socket**, and `make test`
  passes `-daemonize`, which QEMU for Windows does not support. The scripted
  test flow — the only test flow this project has — does not run natively.
- The Makefile and the git hook are POSIX shell.

So:

```powershell
wsl --install -d Ubuntu          # then reboot, open Ubuntu
```

and inside the Ubuntu shell, install the Linux packages above and clone the
repo **into the WSL filesystem** (`~/os8088`, not `/mnt/c/...`) — builds on
the Windows drive are slow and mangle file modes.

WSL2 on Windows 11 shows GUI apps natively, so `make run` opens a real QEMU
window. On Windows 10 without WSLg, stick to `make test` plus screendumps.

If you genuinely want a native Windows build, `nasm`, `python3` and QEMU all
exist for Windows and `make` works under MSYS2 — but you are on your own for
the test tooling, and please don't send patches that break the POSIX path.

## 2. Install an agent

### Claude Code

```
npm install -g @anthropic-ai/claude-code
```

or the native installer — macOS/Linux/WSL:

```
curl -fsSL https://claude.ai/install.sh | bash
```

Then run `claude` from the repo root. It picks up
[CLAUDE.md](CLAUDE.md) automatically: the hard rules, the build commands, the
testing quirks. Docs: <https://docs.claude.com/en/docs/claude-code>.

Useful in this repo:

- `/init` is already done — don't regenerate CLAUDE.md, edit it.
- Plan mode (shift-tab) before anything touching the scheduler or the window
  manager. Those two break in ways that only show up as a hang.
- `claude --permission-mode acceptEdits` if you're tired of approving edits to
  `.inc` files; keep approvals on for `Bash` so you see what it boots.

### Codex

```
npm install -g @openai/codex      # or: brew install codex
```

Then run `codex` from the repo root. Codex looks for `AGENTS.md`, which this
repo doesn't ship — the agent brief lives in `CLAUDE.md`. Either open your
session with:

> Read CLAUDE.md and SPEC.md before touching anything. They are binding.

or, in your own clone only, make an untracked pointer:

```
ln -s CLAUDE.md AGENTS.md     # don't commit this
```

Docs: <https://github.com/openai/codex>.

### Any agent

The three files that matter, in order:

| file          | what it is                                                  |
|---------------|-------------------------------------------------------------|
| `SPEC.md`     | the binding contract — every symbol, register contract, constant and data layout. **Update it before changing an interface, not after.** |
| `CLAUDE.md`   | the working brief — hard rules, build/test commands, the quirks that cost someone an afternoon. |
| `README.md`   | what the OS does and how, for orientation.                   |

## 3. Rules an agent will break if you don't tell it not to

These are in `CLAUDE.md`, but they're the ones worth repeating to a model that
has read a lot of modern x86:

- **8086 only.** `cpu 8086` plus `-w+error` means NASM rejects anything newer:
  no `pusha`/`popa`, no `push imm`, no `shl reg, imm` other than 1 (use CL), no
  `movzx`, no 32-bit registers. If the build fails with a warning-as-error
  about the CPU level, the agent reached for a 186+ instruction.
- **Tiny model.** CS = DS = SS = 0x1000 for the kernel *and* every loaded
  program. All calls are near. ES is scratch but must be restored.
- **Register discipline.** Every public routine preserves all registers except
  its documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG and `cld`
  before string ops. Critical sections are `pushf`/`cli` … `popf` — never
  `cli` … `sti`, which would enable interrupts inside a caller that had them
  off.
- **.bss discipline.** Big buffers go in `section .bss`. NASM's section state
  persists across `%include`, so any `.inc` that opens `.bss` must switch back
  to `.text` before it ends, or the *next* include's code lands in .bss and
  vanishes.
- **Label hygiene.** One flat namespace. Every module-internal label carries
  its module prefix (`vga_`, `mou_`, `sch_`, `wm_`, `inst_`, `menu_`, `ui_`,
  `dsk_`, `ld_`, `fm_`, `ico_`, `desk_`, `dock_`) or is a NASM local label.
- **Memory budget.** Kernel image + .bss must stay below offset 0xA000, where
  the loaded-program pool starts. A build-time assertion in `kernel.asm` fails
  the build if you cross it. When it fires, the fix is smaller data, not a
  bigger budget.
- **Don't edit the dead modules.** `kernel/video.inc`, `keyboard.inc`,
  `string.inc`, `gfx.inc` and `kernel-shell.asm.bak` are relics of the
  pre-GUI text shell and are no longer included by anything. Agents love
  finding them and "fixing" them.

## 4. Verifying — the part that isn't optional

There are no unit tests. Testing means booting the OS and driving it.

```
make test                                                # headless + QMP socket
python3 tools/mouse.py build/qmp.sock to 42 8            # File in the menu bar
python3 tools/mouse.py build/qmp.sock down               # menus need a press...
python3 tools/mouse.py build/qmp.sock to 60 27           # ...drag onto the item...
python3 tools/mouse.py build/qmp.sock up                 # ...and a release
python3 tools/mouse.py build/qmp.sock click 180 150      # ordinary clicks
python3 tools/qmp.py  build/qmp.sock 'sendkey h'
python3 tools/qmp.py  build/qmp.sock 'screendump /abs/path/shot.ppm'
python3 tools/qmp.py  build/qmp.sock 'quit'              # or: kill $(cat build/qemu.pid)
```

Then *look at the screendump*. Both Claude Code and Codex can read the image
back — convert it if your agent prefers PNG (`sips -s format png shot.ppm`
on macOS, `convert shot.ppm shot.png` with ImageMagick elsewhere).

Quirks that have cost real time, all of them still true:

- **Never inject raw HMP `mouse_move`.** QEMU's msmouse backend truncates
  large deltas — big negative ones flip positive. Always go through
  `tools/mouse.py`, which chunks moves to ≤60px and derives an absolute
  position by pinning against the kernel's edge clamp first.
- **Menus need press / move / release**, not `click`.
- **Double-clicks** compare birth ticks with a ~0.5s window, so two separate
  `mouse.py click` invocations are too slow. Position with `mouse.py to X Y`,
  then send both clicks over one connection:
  `python3 tools/qmp.py build/qmp.sock 'mouse_button 1' 'sleep 0.08' 'mouse_button 0' 'sleep 0.12' 'mouse_button 1' 'sleep 0.08' 'mouse_button 0'`.
- **Crop before concluding nothing happened.** One revealed 16px Minesweeper
  cell is invisible in a full 640×480 screendump. Agents will confidently
  report "the click was lost" from a thumbnail.
- **Boot is clean** — nothing is running. Anything you want to click has to be
  launched from a menu first.
- **Both geometries.** Every image is built twice, 1.44MB (QEMU) and 360KB
  (86Box / a real XT). If you touched the boot path or the FAT driver,
  check both.
  Only QEMU is routinely verified; `vm/xt/86box.cfg` keys are best-effort
  guesses and 86Box rewrites its own preference keys on exit.

A pull request that says "builds cleanly" but was never booted is not
verified. Say what you actually ran.

## 5. Prompts that work

Vague asks produce plausible assembly that hangs the machine. Concrete ones,
with the contract named, produce patches:

> Read SPEC.md §7 and kernel/sched.inc. Add a task-priority field to the task
> record. Update SPEC.md's task-record layout table first, then the code. Keep
> it 8086-only, preserve register discipline, and rebuild with `make` — the
> size assertion has to still pass.

> Write a new loadable package in apps/, modelled on apps/hello/. It should
> register one window that draws a rotating line. Use apps/os88api.inc for
> every kernel call, remember packages are relocatable so addresses may only
> appear as whole 16-bit words, and append it to the Makefile's apps-disk
> package list (the root-directory order is pinned — mines first, hello
> second, and so on — because tests click files by row).

> `make test`, open File → Note Pad, type "hi", screendump it, and show me the
> cropped text area. Use tools/mouse.py, not raw mouse_move.

And when it goes wrong, the two debugging tricks that pay off here: a
`cli`/`hlt` at a suspect offset to see whether execution reaches it, and
`tools/qmp.py`'s `xp` for reading guest memory. Note that `nasm -l` lists
.bss addresses *section-relative* — the real guest address is the listing
offset plus the kernel text size; getting that wrong reads a permanent 0x00
and looks like a broken feature.

## 6. Before you commit

```
make clean && make          # both geometries, no warnings (-w+error is on)
```

- **Update SPEC.md in the same commit** if you changed an interface, a
  constant, or a data layout. SPEC.md is the contract; drift is the one bug
  that costs everyone.
- **Enable the secret-scan hook** if you haven't: `git config core.hooksPath
  .githooks`, and install gitleaks. Without gitleaks on PATH the hook warns
  loudly and lets the commit through rather than making commits impossible;
  with it, credential-shaped data is refused. `SKIP_GITLEAKS=1 git commit`
  bypasses it deliberately (for fixtures that are *meant* to look like keys).
- **Don't commit `build/`.** It's gitignored; keep it that way.
- **No vendored third-party code.** Everything in the OS is hand-written and
  the whole tree is MIT under one license file. A dependency would break that.

Commit messages here are a subject line that says what changed, then a
paragraph or two on *why* and what it cost. Look at `git log` for the shape.
If an agent wrote the patch, a `Co-authored-by:` trailer is welcome and
normal — several commits in this repo have one.

## 7. Pull requests

Fork, branch, open the PR against `main`. `.github/CODEOWNERS` auto-requests
review from @jggonz.

A good PR body says:

- what changed and why;
- which SPEC.md sections moved;
- **how you verified it** — the exact commands, and a screendump if it's
  visible. Screenshots are the review currency in a project like this;
- whether you checked the 360KB build or only QEMU.

Things that won't merge: 186+ instructions, anything that breaks the 256KB /
0xA000 budget, vendored code, changes to interfaces without the matching
SPEC.md update, and features that only work in QEMU when they didn't have to
be.

## Questions and security

Ordinary questions and ideas: open an issue. Anything exploitable against a
*host* machine — the Python tooling parses untrusted `.o88` packages and
floppy images — goes through private vulnerability reporting instead; see
[SECURITY.md](SECURITY.md). Guest-side findings ("a package can overwrite the
kernel") are by design and belong in issues, not advisories.
