# Unknown extensions: what SPEC.md 54.4.3 left open

**OPEN, and nothing here is built.** SPEC.md 54.4.3 shipped the case it was
asked for: a DOUBLE-CLICK on a file whose extension no table holds sweeps every
live volume's `ASSOC.DAT` (`assoc_sweep` asking `assoc_tryext`) before giving
up. It cost 52 bytes of `.cold`, resident. Two things were deliberately left
out of that change to keep it small. They are written down here, costed, so
that whoever picks one up does not have to rediscover it.

Figures are against `assoc-find` at build 410 (`kern_big` `.cold` 41,012).
Every byte figure below is an ESTIMATE, counted from the instruction lengths
of the code it would add. Nothing has been assembled.

---

## 1. A PROGRAM asking for a document does not search

`OSAPI_PKG_START` with a document and no program name (SPEC.md 21.5.3) ends
up in `ld_pkg_byname`'s `.byext` arm (`kernel/loader.inc`). That arm asks
`assoc_slot_of` once and answers `LD_EBAD` on a miss. That is exactly the
double-click's behaviour before 54.4.3. So `OPEN HELLO.TEX` from a package on
a machine that has not opened B: yet still fails, even though a double-click
on the same file now works.

**The change is the double-click's, one layer along.** `.have` already stores
AL into `[assoc_dapp]` and calls `assoc_run_x`, and `assoc_run_x` already
treats `ASSOC_FIND` as "sweep, then locate". So the arm needs only to post
the sentinel when the name has an extension:

```
.byext:
    mov si, assoc_doc
    call assoc_slot_of
    jnc .have
    call assoc_ext_of           ; no extension: nothing to look for
    jc .nodoc
    mov al, ASSOC_FIND
.have:
```

**About +7 bytes, in `.cold` and resident.** No kernel routine is new.

**The one thing to decide first is the VERDICT.** Today `.byext`'s miss
answers `AL = LD_EBAD`, which is how a caller learns that nothing on this
machine opens the file. After the change, a search that comes back empty
reports through `assoc_run_x`'s own `ld_say_status` (a toast), and
`ld_pkg_byname` returns `AL = 0` after the call, as it does on every
association launch. The caller would lose the refusal. Two ways out:

- `assoc_run_x` leaves the verdict in `[ld_status]` on the `.noassoc` path
  (it already does, since `ld_say_status` stores it), and `ld_pkg_byname`
  answers `AL = [ld_status]` instead of a constant 0. That is about +3 bytes.
  It also changes what a successful association launch answers, if
  `[ld_status]` is not `LD_OK` then. Check that before taking it.
- Or accept `AL = 0` plus the toast as the answer. SPEC.md 21.5.3 already
  calls the association route's answer "thinner", so this may be acceptable.
  It would need saying there.

**The gate** is `tests/assocfind.py`'s shape driven through a package instead
of a mouse. `tests/pkgrun.py` already launches through `OSAPI_PKG_START` and
is the place to add a leg.

---

## 2. The toast for an empty search still says `Load failed`

When no volume claims the extension, `assoc_run_x`'s `.noassoc` reports
`LD_EBAD` through `ld_say_status`. On a shipping build that reads `Load
failed` (`Bad package` needs `LDDIAG=1`, `kernel/files.inc`'s `fm_stattab`).
It is the verdict the double-click always gave, which is why 54.4.3 added no
string. But it is a loader's word for a question the loader was never asked:
nothing was loaded and nothing failed. What is true is that no program on any
disk the machine could reach claims `.ZZZ`.

**The message that fits** is `Nothing opens .ZZZ`: 18 characters and a NUL,
inside `TOAST_MAX`'s 24. It names the extension, and the extension is what
the user can act on.

**The cheap way is to reuse `assoc_pnbuf`.** That buffer is `'Needs '` plus 13
bytes, 19 in all, which is exactly `Nothing opens .ZZZ` and its NUL. The catch
is that `'Needs '` is assembled in and never rewritten: `assoc_progname`
writes only past it. So sharing the buffer means one of two things
(**since kernel size pass 5 the 13 bytes ARE `assoc_fnb`**, the program
name `assoc_locate` builds, and `assoc_progname` is gone - so a second
message written there would also overwrite a name the `.nofile` toast reads;
re-price this with that in mind):

- both toast paths write their own prefix, costing `assoc_progname` about +6;
  or
- the new message is laid out so it does not overlap the constant.
  `'Needs '` sits at +0..+5, so there is no such layout. This is the reason to
  price the first option rather than hope for the second.

After that, the extension comes from `assoc_extb` (3 bytes, space-padded), as
left by `assoc_post`'s `assoc_ext_of`. `.noassoc` then shows the toast the way
`.nofile` already does (`mov si, ...` / `push ds` / `pop es` / `xor cx, cx` /
far call to `cw_toast_show`), in place of `mov al, LD_EBAD` /
`call ld_say_status`.

**About +25 to +35 bytes in total.** The string is about 14 bytes of `.text`
(`'Nothing opens .'`), and `.text` is the section that binds `KERN_CODE_MAX`.
The copy and the call are about 15 to 20 bytes of `.cold`. Where the string
lives is the choice that matters, not the total. The cheapest honest version
may be to keep `LD_EBAD` and only change which string it maps to on this one
path, and that needs a table index of its own.

**Not to be confused with** `Needs TRACKER.O88` (SPEC.md 54.4.1). That is the
opposite failure: the extension IS claimed, but the program it names could
not be found.
