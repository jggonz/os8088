# Security Policy

## What is and isn't in scope

os8088 is a hobby operating system for the Intel 8086, written in real-mode
assembly and booted from a floppy image. It has no memory protection, no
privilege separation, and no network stack. The kernel and every loaded
package share one 64KB segment, so a package can read or write any byte of
the machine, including the kernel itself. That is the 1984 design being
reproduced, not a defect — please don't report it as one.

Reports that *are* in scope:

- **Host-side tooling** (`tools/os88pkg.py`, `tools/os88disk.py`, `tools/qmp.py`,
  `tools/mouse.py`). These parse package binaries and floppy images. A crafted
  input that leads to code execution, arbitrary file writes, or path traversal
  on the *host* machine is a real vulnerability.
- **The release pipeline and published artifacts.** Anything that would let a
  third party substitute a floppy image or defeat the checksums published on
  os8088.com.
- **Repository supply chain.** GitHub Actions workflows, the tracked git hooks
  in `.githooks/`, or anything that executes on a contributor's machine as a
  side effect of cloning, building, or testing.

Guest-side findings are welcome as ordinary issues if they're interesting —
they're just not handled as security reports.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting: the **Security** tab →
**Report a vulnerability**. That opens an advisory visible only to maintainers.
Please don't open a public issue for anything exploitable against a host
machine.

Include:

- what you did and what happened,
- the affected file and commit,
- a minimal reproducer — a crafted `.o88` package or floppy image is ideal,
- the emulator and host OS, if relevant.

## What to expect

This is a single-maintainer hobby project, so there is no SLA. Realistically:
an acknowledgement within about a week, and a fix in the next release if the
report holds up. You'll be credited in the advisory unless you ask not to be.

## Supported versions

Only the tip of `main` and the most recent release. There are no backports to
older releases.
