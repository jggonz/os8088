# The committed CP/M cache

`cpmcache.zip` holds every file `tools/getruncpm.py` and `tools/getcpmsw.py`
pin, so a clean `make runcpmdisk`, `make allapps` or `make live` extracts
them instead of downloading them (SPEC.md §74.6.1).

| member | what | whose | pinned by |
|---|---|---|---|
| `runcpm/CCP/CCP-DR.60K`, `runcpm/DISK/A0.zip`, `runcpm/DISK/1STREAD.ME`, `runcpm/LICENSE` | RunCPM's CCP, master disk, read-me and licence, at commit `e698e8ab59c2` | RunCPM (Marcelo Dantas / Mockba the Borg, MIT); the programs on the master disk are their own authors' | `getruncpm.PINNED` |
| `cpmsw/<DRIVE>/<USER>/<NAME>` | the CP/M games and applications the RUNCPM disks carry beside the master disk | each program's own authors - Yahoo Software, Borland, MicroPro and others | `getcpmsw.PINNED` |

## Where they came from

- **RunCPM's files**: `https://raw.githubusercontent.com/MockbaTheBorg/RunCPM/<commit>/`
  (`getruncpm.RAW`).
- **The software**: the public RunCPM software collection on **Google Drive**,
  folder `1FZAcIP9_Hf0lCL_FDs1qJCrk2GHdirv8` (`getcpmsw.COLLECTION`), one
  download per file by the id in `getcpmsw.PINNED`. Drive answers each with a
  virus-scan form that has to be posted back, and eighty-odd of those were
  minutes of every clean build - which is why this zip exists.

## Why they are committed, when nothing else third-party is

`CONTRIBUTING.md` §6 says nothing third-party is committed. **This is a
stated, user-decided departure from that rule, for this one zip**, in the
shape `apps/c64/rom/` already took. The files are not ours and are not MIT
under this tree's licence; they are carried here only so the build does not
have to reach the network for them.

## The pins stay the authority

The zip is a transport, not a second source of truth. Every member is checked
against its SHA-256 and size on the way out, exactly as a download is, and
`tools/cpmcache.py --check` (the `cpmcache` fast row) fails the build if the
zip lacks a pinned file, contradicts one, or holds a file nothing pins.

## When a pin moves

    python3 tools/getruncpm.py -o build/runcpm-disk   # fetches what the zip lacks
    python3 tools/getcpmsw.py -o build/cpmsw
    python3 tools/cpmcache.py --pack                  # rebuild the zip

`--pack` is deterministic (sorted members, a fixed 1980 timestamp, fixed
attributes), so repacking unchanged inputs produces the same bytes.
