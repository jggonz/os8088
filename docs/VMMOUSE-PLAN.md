# The VMware absolute pointer — `vmmouse`, for the browser

**Research document, not a contract — and now IMPLEMENTED, in a different
shape from the one studied here.**
> The study below was written before the code and **plans a resident feature
> in `kernel/mouse.inc`, gated on `[cpu_tier]`. That is not what shipped.**
> Everything it says about the protocol, the packet, the contest and the
> 8042 still holds; everything it says about where the code LIVES was
> overtaken at review — see §15. **SPEC.md §9.11 is the binding account**;
> where the two disagree, the SPEC wins.

Read it before touching `kernel/mouse.inc`'s pointer path or the `cpu 386`
island.

The ask, in the requester's words:

> When running in the browser it would be good if it could use absolute mouse
> coordinates as it wouldn't require mouse grab then. os8088 has to work on
> real 8088s as well as in the browser. On the browser it runs in v86, which
> supports the VMware mouse backdoor. Recent v86 (commit `a94c575b`, June
> 2026) emulates the VMware mouse backdoor on I/O port `0x5658`, and the
> browser side already feeds it absolute canvas coordinates with no pointer
> lock, hides the host cursor, and suppresses the context menu whenever a
> guest enables absolute mode. os8088 just needs a guest-side driver that
> speaks the protocol.

The website that hosts the browser build is the sibling `os8088-web` repo
(README's closing section); it vendors v86. Nothing in *this* repo changes on
the web side — this is purely the guest driver.

---

## 0. The verdict, up front

**Small, resident, `kern_big` only, and gated twice — once at assembly and
once at run time — behind the tier the kernel already publishes.**

| | decision |
|---|---|
| where the code lives | `kernel/mouse.inc`, a new section after §9.9's PS/2 half, wholly inside `%ifdef KERN_BIG` |
| the 32-bit instructions | a scoped `cpu 386` island closed by `cpu 8086`, behind `cmp byte [cpu_tier], CPU_386` — **`XMEM.DRV`'s pattern (SPEC.md §41.9 rule 2), and the first such island in `kernel/`** |
| detecting the browser | there is nothing to detect — **the backdoor probe is the test**. Present ⇒ use it; absent (every real XT, and a real 386 with no hypervisor) ⇒ the serial/PS/2 path runs unchanged |
| how it joins §9.5's contest | as row `MOU_VMROW` = 6, and it wins outright at boot: a working backdoor is a *probe*, stronger than the serial identify burst, so it settles `[mou_port]` before the serial dance and `mou_lockon` retires the UARTs and the aux port |
| the pointer position | absolute 0..0xFFFF from the device, scaled to `[vid_w]`/`[vid_h]` with **one `mul`**, then `mou_clamp`, then the *unchanged* back half of `mou_apply` — events, `[blk_act]`, cursor draw |
| polling | once per UI-task pass and inside the drag / grow / menu-track sub-loops, behind one `cmp byte [vmm_on], 0`. The poll only ever *runs* under emulation, where the CPU is ~1000× the target — so its cost is not the target machine's |
| cost | est. 300–450 B of `.text` on `kern_big` only; **check `KERN_BUDGET` headroom before committing** (§8) |

One binary still boots on a 4.77 MHz XT: on an 8086 `[cpu_tier]` is
`CPU_8086`, the run-time gate fails at its first `cmp`, and the `cpu 386`
island is never entered. On a real 386+ with no hypervisor the probe reads
back a non-magic `EBX` and the fallback is byte-for-byte today's code.

---

## 1. Why the probe *is* the detection

VMware put the backdoor on port `0x5658` ('VX') precisely because no real PC
hardware decodes it. The probe — `EAX = 0x564D5868`, `ECX = 10`
(`GETVERSION`), `EDX = 0x5658`, `in eax, dx` — is "present iff `EBX` comes
back `0x564D5868` and `EAX != 0xFFFFFFFF`". On bare metal the `in` reads an
undriven port: the bus floats or returns `0xFF`s, `EBX` is untouched, the
test fails cleanly. So:

- **No v86 detection, no VMware-vs-VirtualBox branch, no CPUID hypervisor
  bit.** The one probe answers the only question that matters: *can I speak
  this protocol right now.*
- The residual risk is a real 386-era machine whose chipset aliases
  `0x5658` onto a register that happens to answer with the magic in `EBX`.
  This is the same class of risk §9.9.2 takes on port `0x64` and rules
  acceptable; `0x5658` is a far safer bet than `0x64` because it was chosen
  to be dead. Note it, do not guard against it.
- It must still be **behind the CPU gate**, because the probe *instructions*
  are 32-bit. `0x66` is not a prefix on an 8086 and the encoding would
  execute as something else entirely. `[cpu_tier] == CPU_386` (§60, already
  detected — the FLAGS test in `cpu_detect` reports `CPU_386` for a 386 and
  everything above it, i.e. exactly "386+") is the assembly-island's
  run-time partner.

## 2. The 386 code — an island, not hand-encoded prefixes

The intake brief suggests assembling the backdoor calls as raw
`db 0x66, 0xED` etc. **The tree already has a sanctioned way to do this and
it is cleaner**: `drivers/xmem/xmem.asm` wraps every 386 instruction in

```
cpu 386                         ; ---- 386-only island ----
    ...
cpu 8086                        ; ---- island closed ----
```

behind a `cmp byte [xm_tier], CPU_386`. SPEC.md §41.9 rule 2 governs it:
*an island is assembly-time permission only — one reached on a 286 is an
illegal-opcode trap on a machine with no handler.* The run-time `cmp` is
what makes sure it is never reached below tier 2.

**This will be the first `cpu 386` island in `kernel/`** (today every line
under `kernel/` is `cpu 8086`; all 386 code in the project is quarantined in
the `XMEM.DRV` overlay). That is a real fact for the reviewer, but the rule
that governs the island is not new — it is proven in `xmem.asm` — and the
island here is a dozen instructions, not a memory-copy engine.

The backdoor helper is one routine. It takes the command in `CX`, the in/out
`EBX` where the protocol wants it, and returns the four result registers to
fixed `.bss` dwords the 8086 code reads as word pairs:

```
cpu 386
vmm_bd:                         ; CX = command, [vmm_ebx] = EBX in
    push eax
    push ebx
    push ecx
    push edx
    mov  eax, 0x564D5868
    movzx ecx, cx
    mov  ebx, [vmm_ebx]
    mov  edx, 0x5658
    in   eax, dx
    mov  [vmm_eax], eax
    mov  [vmm_ebx], ebx
    mov  [vmm_ecx], ecx
    mov  [vmm_edx], edx
    pop  edx
    pop  ecx
    pop  ebx
    pop  eax
    ret
cpu 8086
```

`movzx` is a 386 instruction and legal *inside* the island. The `push
eax`…`pop` bracket keeps the high halves of the caller's 32-bit registers
intact — a real-mode 8086 caller has no high halves it cares about, but the
BIOS re-entered by IRQ0 between our `push` and `pop` might, and this is
`xmem.asm`'s own reasoning (its §"BIOS re-arm" note) applied one level down.
Every caller of `vmm_bd` is plain 8086.

## 3. The sequence — probe, enable, poll

Straight transcription of the published Linux/Xorg `vmmouse` driver. All of
it goes through `vmm_bd`.

**Probe** (`vmm_init`, boot, once, tier-2-gated):

1. `CX = 10` (`GETVERSION`), `EBX = 0`. Present iff `[vmm_ebx] == 0x564D5868`
   and `[vmm_eax] != 0xFFFFFFFF`.

**Enable** (`vmm_enable`, on the probe and again after any disable):

2. `CX = 41` (`ABSPOINTER_COMMAND`), `EBX = 0x45414552` → REQUEST.
3. `CX = 39` (`ABSPOINTER_DATA`), `EBX = 1` — read one dword; expect
   `[vmm_eax] == 0x3442554A` (the version id). A mismatch means the backdoor
   is not really there — bail to the serial path.
4. `CX = 41`, `EBX = 0x53424152` → ABSOLUTE.

**Poll** (`vmm_poll`, per §5's cadence, only when `[vmm_on]` is set):

5. `CX = 40` (`ABSPOINTER_STATUS`), `EBX = 0`.
   - `([vmm_eax] & 0xFFFF0000) == 0xFFFF0000` → v86 disabled the backdoor
     (it does this on its own queue overflow). Clear `[vmm_on]` briefly,
     re-run step 2–4, return. Next pass resumes.
   - `count = [vmm_eax] & 0xFFFF` is dwords queued. Need `>= 4` for a
     packet; `< 4` → nothing to do this pass.
6. `CX = 39`, `EBX = 4` — read the packet: `[vmm_eax]` = status,
   `[vmm_ebx]` = x, `[vmm_ecx]` = y, `[vmm_edx]` = z (wheel, signed).
   Loop back to step 5 and drain everything queued (bounded by the queue
   size, like `ui_task`'s `EVQ_CAP` drain).

**Status word decode:**

| bits | meaning |
|---|---|
| `0x20 / 0x10 / 0x08` | left / right / middle button |
| `0x00010000` | RELATIVE_PACKET — x,y are signed deltas, not positions (§7) |
| x, y (absolute) | 0..0xFFFF, scale: `screen_x = (x * [vid_w]) >> 16` |
| z | wheel delta, signed — **dropped** (§10 has nowhere to put it, exactly as §9.9.5 drops the PS/2 wheel) |

## 4. Joining the contest

`vmm_init` runs **first** in `mouse_init`, before the serial UART probe. On
success it does what a settled serial packet does in `mou_claim`:

```
mov byte [mou_port], MOU_VMROW      ; 6 — past MOU_P2ROW (4)
mov byte [mou_line], 0xFF           ; MOU_P2LINE's trick: not a master-8259
                                    ; bit, so mou_lockon masks the serial
                                    ; lines and none of ours (there are none)
mov byte [mou_seen], 1
mov byte [mou_ptr],  1              ; the machine HAS a pointer → the keyboard
                                    ; mouse (§9.6) stands down
mov byte [vmm_on],   1
```

and then **returns from `mouse_init` early**, skipping:

- the UART probe / hook / program / reset-low-hold-raise / **identify-burst
  drain** — the last of which is `MOU_IDWIN` ≈ 1 s of boot (§9.4.1). On the
  browser build that second is pure waste today; this removes it.
- the PS/2 probe (§9.9) and its ~110 ms handshake.
- the `int 09h` keypad-5 hook — with a real pointer the keyboard mouse never
  runs, so its one-button fallback is dead weight.

`mouse_unhook` then has nothing serial to undo (nothing was hooked), and
`mou_hotplug` is `cmp byte [mou_seen], 0` / `jne` on its first call —
retired for the session, as with any settled mouse.

**The simpler, lower-risk staging** (recommend for the first cut): let
`mouse_init` run the whole serial dance as today and call `vmm_init` where it
currently calls `mou_p2_init`. On success, `vmm_init` sets the five bytes
above and calls `mou_lockon` to silence the UARTs. Costs the ~1 s identify
window on the browser but touches far less of a load-bearing routine. Take
the early-return optimisation as a follow-up once the apply path is proven.

## 5. The apply path — one new entry, shared tail

`mou_apply` (§9.9.3) today is: `add` the signed deltas to `[mouse_x]`/
`[mouse_y]`, `mou_clamp`, then the protocol-agnostic tail — `[blk_act]`,
`[cur_shchk]`, `sch_wake_ui`, the button/event decode, the draw-or-defer.

Add `mou_apply_abs`, entered with `AX` = absolute x (already scaled to
pixels), `BX` = absolute y, `CL` = buttons in `[mouse_btn]` order:

```
mou_apply_abs:
    call mou_clamp              ; still — bounds, and multi-display safety
    mov  [mouse_x], ax
    mov  [mouse_y], bx
    jmp  mou_apply.tail         ; the label today at "mov byte [cur_shchk],1"
```

The scaling is one `mul` per axis in `vmm_poll` (8086 `mul` is 16×16→32 in
`DX:AX`; take `DX` = `(x * vid_w) >> 16`). `x` ≤ 0xFFFF and `[vid_w]` ≤ 640,
so the product never overflows 32 bits.

A **RELATIVE_PACKET** (status `0x00010000`) carries deltas, not positions —
v86 only emits these under browser pointer lock, which os8088 never engages,
but handle it defensively: sign-extend the low 16 bits and `jmp mou_apply`
(the existing delta entry) instead. One `test` / `jnz` in `vmm_poll`.

A second copy of the event/cursor tail is a second place to get §9's binding
both-buttons-queues-left-only fall-through wrong. There is exactly one tail.

## 6. Polling cadence

The poll must feel like a mouse, so 18.2 Hz (the tick section where
`mou_hotplug` lives) is too slow for dragging a window. It goes:

- once at the top of `ui_task.loop`, before `.keys`;
- beside every `call kbm_pollm` — the drag (`ui_drag`), grow (`ui_grow`) and
  menu-track sub-loops that spin on `task_yield` and poll `[mouse_btn]`
  directly.

Guarded by `cmp byte [vmm_on], 0` / `je` — **one byte compare per pass on
every machine that is not in a browser**, which is the same price
`mou_hotplug` and the keyboard-mouse tail already pay and defend.

The far more important point: **`vmm_poll`'s body only ever executes under an
emulator**, where the CPU runs ~1000× the 4.77 MHz target. PERFORMANCE.md's
budget is about the XT; this code is unreachable there. The two backdoor
`in`s per pass are not a cost anyone measures.

## 7. What is deliberately not done

- **No wheel, no middle button** — §9.9.5's reasons verbatim; §10's event
  set has three records and no room.
- **No Control Panel row, no `SYSTEM.CFG` key, no build knob** — §9.5's rule:
  the machine answers faster than the user, and a stored answer is wrong
  after the environment moves. `vmm_on` is discovered every boot.
- **No multi-display scaling.** v86 presents one framebuffer; `[vid_w]`/
  `[vid_h]` are the whole screen there. A dual-display hypervisor guest is
  not a machine os8088 targets — if it ever is, the scale target becomes the
  per-display geometry and `mou_clamp`'s `.multi` arm already exists.
- **No `int 15h AH=C2h`** — the BIOS absolute-pointer path, absent from v86
  and a far call into unknown ROM in the pointer path regardless (§9.9.5).
- **No relative-mode support beyond the defensive route** in §5 — os8088
  never locks the pointer, so v86 never sends it relative packets in
  practice.

## 8. Memory

`kern_big` only. The nine-ish `.bss` bytes (`vmm_on` + four result dwords +
`vmm_ebx` scratch = 19 bytes) and the code (est. 300–450 B `.text`) are all
inside `%ifdef KERN_BIG`, so `kern_small` — the 128–256 KB XT product, every
one an 8086 — pays **nothing**, not even the published-state shape §9.9 had
to leave behind (this state is not published in SPEC.md §9.4.2, so it need
not exist on `kern_small` at all).

**Open question for whoever spends the budget:** §9.9.5 records `kern_big`'s
image rung at one 512-byte step of spare and `KERN_CODE_MAX` at 3,873 bytes.
300–450 B of `.text` fits `KERN_CODE_MAX` but **lands on that last image
rung** — `KERN_SIZE` crosses a 512 boundary and `KERN_BUDGET` goes from one
step of spare to zero. Run `tools/kernsize.py` before and after. If it is the
last rung, this is a decision to take with the requester (CLAUDE.md's memory
rule), and the honest alternative is §9's on-demand-module test: is this a
`CTRL.DRV`-style loadable? It nearly qualifies — the browser build could
carry a `VMMOUSE.DRV` the boot loads only when `[cpu_tier] == CPU_386` — but
it fails on *frequency*: the poll is per-pass resident work and the inject
path (`mou_apply_abs`) is resident kernel with no API slot a driver could
call. A driver would need a resident poll hook and a resident inject shim
anyway, i.e. most of §5–§6 stays resident and the driver saves only the
dozen-instruction island. Not worth a `.DRV` at that ratio — but revisit if
the island grows.

## 9. Testing

- **v86 / the browser build** — the real target, and *not* assertable
  (docs/TESTING.md): boot `os8088-web`, move the pointer with no click first,
  confirm the host cursor is hidden and the OS arrow tracks it 1:1 with no
  grab. This is a look, like `xt-weave-256`.
- **QEMU** — QEMU has a `vmmouse` device and a `vmport` (`-machine
  vmport=on`, on by default for `pc`). **Unverified** whether v86's
  poll-only flavour and QEMU's (which is wired to the PS/2 aux device and
  needs `-device vmmouse`) are close enough for one driver to drive both. If
  they are, `tests/vmmouse.py` under `make test` can assert `[mouse_x]`/
  `[mouse_y]` track a backdoor-injected absolute position — worth an hour to
  find out, because it would make this the rare browser feature with a CI
  gate. If not, QEMU stays on the serial mouse and this is browser-only.
- **MartyPC** — no backdoor of any kind; the probe fails and the serial
  path runs. The regression to check here is that nothing changed:
  `tests/suite.py`'s existing mouse rows must pass untouched.
- **`make xt` / `make test` (tier 0 / no hypervisor)** — identical boot and
  desktop to today; the `cpu 386` island is never entered. This is the
  regression that matters most, §41.10's framing.

## 10. SPEC / doc changes when this lands

- **New SPEC.md §9.11** — "The VMware absolute pointer": the protocol
  sequence, the `MOU_VMROW` = 6 contest entry, the `cpu 386` island rule
  pointer (§41.9 rule 2), the cadence, the cost accounting in §9.9.5's idiom,
  and the "what is not done" list from §7.
- **SPEC.md §9's public-symbol list** (~line 7850) gains `vmm_poll` beside
  `mou_hotplug` as "the UI task's per-pass call", noting it is `kern_big`
  and tier-2 only.
- **CLAUDE.md's document table** — a row for this file:
  *"docs/VMMOUSE-PLAN.md | the browser's absolute pointer (§9.11) — why the
  backdoor probe is the whole of 'detect the browser', and the first
  `cpu 386` island in the kernel"*.
- **`MOUDIAG=1`** could gain a row (backdoor present? version id? packets
  drained?) on the §9.9.6 model — optional, and only if a field report ever
  needs it.
- Regenerate `docs/INDEX.md` (`tools/os88index.py`) — no new API slot, so
  this is just the SPEC-heading sweep.

## 11. Work plan

1. `vmm_bd` island + `vmm_init` probe, wired where `mou_p2_init` is called.
   Prove the probe fires under v86 and is silent under QEMU/MartyPC/`xt`.
2. `vmm_enable` + `vmm_poll` + `mou_apply_abs`, cadence at the top of
   `ui_task.loop` only. Pointer tracks in v86; nothing regresses elsewhere.
3. Poll in the drag / grow / menu-track sub-loops. Dragging a window in v86
   is smooth.
4. Decide QEMU testability (§9); land `tests/vmmouse.py` or record why not.
5. The `mouse_init` early-return optimisation (§4) — separate commit, its
   own before/after boot-time measurement.
6. SPEC.md §9.11, the doc-table row, `MOUDIAG` if wanted. PR.

---

## 12. What shipped

Branch `feature/vmmouse-absolute`. Landed as one change, steps 1–3 and 6 of
§11 together; the QEMU-testability question (4) and the boot-time
optimisation (5) are deferred as the plan proposed.

**Where it lives.** All resident, `kernel/mouse.inc`, one `%ifdef KERN_BIG`
section after the PS/2 half:

- `vmm_bd` — the `cpu 386` island (the **first in `kernel/`**), closed by
  `cpu 8086`. Command in `CX`, argument in `[vmm_ebx]`, four result dwords
  out. `push eax`…`pop` brackets the 32-bit halves.
- `vmm_enable` / `vmm_init` — REQUEST, version check, ABSOLUTE; the probe and
  the contest settle (`[mou_port]` = `VMM_ROW` = 6, `[mou_seen]`/`[mou_ptr]`/
  `[mou_idany]` = 1, `[vmm_on]` = 1).
- `vmm_poll` / `vmm_read` — drain the queue (`VMM_DRAIN` = 24 bound), scale
  with one `mul` an axis, into `mou_apply_abs`.
- `mou_apply_abs` — sets `[blk_act]`, `mou_clamp`, stores the position, jumps
  to a new `mou_apply.tail` label. **One copy of the event/cursor tail.**
- `VMM_POLL` macro — `cmp byte [vmm_on], 0` / `call vmm_poll`, expands to
  nothing on `kern_small`.

**Wiring.** `mouse_init` calls `vmm_init` where it called `mou_p2_init`; on
success `mou_lockon` retires the UARTs and the PS/2 probe is skipped.
`VMM_POLL` sits in `ui_task`'s `.events` (before the ring drain) and beside
every `kbm_pollm` — `ui_drag`, `ui_grow`, `menu_track`.

**Cost, measured (`tools/kernsize.py`).** `kern_big` `.text` **+459 B**, and
it **crossed the image rung**: 125 → 126 steps of 512, `KERN_BUDGET` spare
8,192 → 7,680 (16 → 15 steps). `KERN_CODE_MAX` 1,332 B left. No guard
raised; `kernbudget` and the fast tier pass. `kern_small` +0 — the module and
all 19 bytes of state are inside the `%ifdef`. This is §8's anticipated rung
crossing; the requester asked for the feature on a branch to try, which is
the decision CLAUDE.md's memory rule wants taken by a person.

**Differences from the plan.**

- Relative packets: the plan said "sign-extend the low 16 bits and
  `jmp mou_apply`". Shipped code also **negates the y delta** — VMware
  relative y is positive-up, like the PS/2 wire (§9.9.3), and `mou_apply`
  wants positive-down. Still a path that never runs without pointer lock.
- `.redo` (backdoor disabled on v86 queue overflow): the plan left `[vmm_on]`
  set and retried each pass. Shipped as written — `vmm_enable` is re-run and
  `[vmm_on]` stays set, the next `STATUS` read catching a still-dead backdoor.
- No `MOUDIAG` row yet — deferred until a field/browser report needs one.

**The QEMU collision — discovered, not planned (§9 of this doc guessed both
ways).** QEMU's `pc` machine carries a `vmport` **and** a `vmmouse` by
default, so `vmm_init` succeeds under a plain `qemu-system-i386` exactly as it
does under v86 — and then wins the contest and retires the `msserial` mouse
that `tools/mouse.py` drives, so `ps2mouse` failed and every mouse-driving
recipe would have. Resolved:

- **Makefile**: `QEMU := qemu-system-i386 -machine pc,vmport=$(VMPORT)`,
  `VMPORT ?= off`. Every recipe that routes through `$(QEMU)` — `make test`
  and the ~8 QEMU tests built on it, plus `functional-check` — is back to the
  serial mouse.
  > **Superseded at review (§15.2).** Folding the machine flag INTO `$(QEMU)`
  > meant a documented `QEMU=` override dropped it — six recipes and four
  > documents tell the reader to replace that variable wholesale, and the band
  > benchmarks' `QEMU="qemu-system-i386 -icount shift=3,sleep=off"` is one of
  > them. The tree now carries `QEMUMACH := -machine pc,vmport=$(VMPORT)` as
  > its own variable, appended at each of the eight use sites.
- **Direct launchers**: `tests/ps2mouse.py`, `tests/heapmap.py`,
  `tests/vgadirty.py` spell `vmport=off` themselves (`rczex` goes through
  `make test`).
- **`tests/vmmouse.py`** (NEW, `full` tier): the one that turns it **on**
  (`vmport=on`, `-serial none` — the backdoor as the only device, like the
  browser). Asserts `cpu_tier` 2, `vmm_on` 1, `mou_port` 6, then injects
  absolute positions through QEMU's `vmmouse` and checks the pointer lands on
  the pixel. **So this browser-only feature does get a CI gate after all** —
  the plan's §9 open question, answered yes.
- SPEC.md §9.11.6 and docs/TESTING.md carry it.

### 13. The freeze — found on `make run VMPORT=on`, fixed

The requester tried `make run VMPORT=on`, and it worked until *"I went to the
A: drive and double click a folder it seems to freeze"*. Two bugs, one visible
symptom:

**13.1 The spin loops.** vmmouse is polled, and §11's `VMM_POLL` sites
(`ui_task`, `ui_drag`, `ui_grow`, `menu_track`) were **not all the loops that
wait on the mouse**. `files.inc`'s `fm_onclick` has its own icon-drag loop
(`.wait` / `.track`), in the cold segment, spinning on `test [mouse_btn]` and
`cw_evq_pop` with **no `kbm_pollm` and no `VMM_POLL`** — it relied entirely on
the serial/PS/2 ISR running in the background. With vmmouse there is no ISR, so
the release never arrived: infinite loop, `[mouse_btn]` stuck at 1, ticks
still advancing.

The fix moved the drain **into `task_yield`** — one `cmp byte [vmm_on], 0` at
its head. Every spin loop in the tree calls `task_yield` (the cold segment via
`cw_task_yield`), so this services all of them, present and future, with no
per-loop `VMM_POLL`. The three sprinkled ones in `ui_drag` / `ui_grow` /
`menu_track` were removed (redundant); `ui_task`'s one stayed, before
`evq_pop`, so a click drains in time to dispatch the same pass. `task_yield`
runs with `IF` on, so two task slices can now be inside `vmm_poll` over the
shared `[vmm_ebx]` scratch — an `xchg` on `[vmm_busy]` serialises them.

**13.2 QEMU disables the backdoor on a `DATA` underflow.** `hw/i386/vmmouse.c`:
if a `DATA` read asks for more words than are queued, QEMU sets
`status = 0xFFFF` and **removes the mouse handler**. READ_ID queues exactly
one word. So the old `vmm_enable` — READ_ID, then a `DATA` of a *guessed* size
to read the version back — could leave the queue at an odd length once real
mouse events were interleaved, and from then on every `vmm_read` was misframed
`[stale, buttons, x, y]`: pointer and button frozen mid-value. Fixed by
**not reading the version back at all** (the `GETVERSION` probe is the gate)
and draining the queue to empty after every enable (`vmm_flush`), plus
`vmm_poll` refusing a `count` that is not a multiple of 4.

**13.3 The multi-device collision.** With `-serial none` gone from
`make run`'s `$(MOUSE)`, the QEMU PS/2 mouse and vmmouse both existed and QEMU
split abs coordinates and button events between them. `make run VMPORT=on` /
`make test VMPORT=on` now force `-serial none` (the Makefile's `MOUSE`), which
is the browser's own shape — v86 has no serial mouse.

Verified under QEMU (`vmport=on`, `-serial none`): open a drive, single-click
selects, double-click opens the folder, drag-and-drop moves a file, a menu
pull-down tracks — all clean, no freeze. `tests/vmmouse.py` now drives a
press / move-while-held / release and asserts `[mouse_btn]` returns to 0 and
the clock keeps advancing.

### 14. v86: the mouse didn't move at all

Tried in the actual browser (v86): pointer completely dead, though QEMU was
fine. **v86's browser mouse handler (`MouseAdapter`) stays disabled until the
guest enables the PS/2 mouse stream** — `may_handle()` returns false on
`!mouse.enabled`, and `mouse.enabled` is flipped only by a `mouse-enable` bus
event, which v86's PS/2 controller sends on the AUX `0xF4` / `0xFF` commands.
os8088's vmmouse path skips `mou_p2_init` entirely, so it never touched the
8042 → no `mouse-enable` → no events, absolute or otherwise. (QEMU and VMware
deliver backdoor events regardless; only v86 gates on this.)

Fixed with **`vmm_p2wake`** in `vmm_init`: send AUX `0xF4` (enable → v86 fires
`mouse-enable`, `MouseAdapter.enabled = true`, host cursor hidden), then
immediately AUX `0xF5` (disable the packet stream so no PS/2 packet is queued
and `int 74h` stays safely unhooked — a stray aux byte would be eaten by
`int 09h` as a scancode). `0xF5` does not clear v86's "mouse enabled" state,
so `mouse-absolute` keeps reaching the backdoor.

**Verified headlessly against the real v86 build** (`../v86`, built with
`make all`): `examples/os8088-smoke.mjs` boots `os8088.img`, shims a minimal
DOM, runs v86's actual `MouseAdapter`, dispatches synthetic `mousemove`
events over the "canvas", and reads the guest's kernel state back —
`MouseAdapter.enabled` is true after boot, and `[mouse_x]` tracks the events
to the pixel. `tests/vmmouse.py` (QEMU) gained a keyboard-survival check
(six keys → BIOS buffer tail +12) since `vmm_p2wake` now pokes the 8042.

To try it: `cd ../v86 && python3 -m http.server`, open
`examples/os8088.html`. After a rebuild,
`cp build/os8088.img ../v86/examples/`.

**"Capture pointer" still works.** Nothing in os8088 needs it — absolute mode
is 1:1 already — but v86's "Lock mouse" button engages pointer lock, after
which v86 stops sending absolute positions and sends **signed deltas with the
`RELATIVE_PACKET` (bit 16) flag**. `vmm_read`'s `.rel` branch routes those to
`mou_apply`'s delta entry (`x` as-is, `y` negated — the wire is positive-up).
Verified in the headless smoke: with capture on, a `movementX/Y` of
`(-12, -6)` × 10 moves the guest pointer exactly `(-120, -60)`. `os8088.html`
carries a Lock-mouse button; `<Esc>` releases.

**Not yet done / next.**

- The `mouse_init` early-return (skip the ~1 s serial identify window on the
  browser build) — separate commit, its own before/after boot measurement.
- `MOUDIAG` row, if a browser report ever needs one.
- The v86 page + smoke test currently live in the `../v86` checkout; they
  belong in the `os8088-web` sibling repo.

---

## 15. The fork: why it is a driver and not kernel code

The plan's §2 chose a resident implementation behind two gates, and §41.9
rule 2 licenses exactly that. It assembled, it worked, and the review that
followed rejected the placement rather than the feature, on two grounds.

**The arithmetic.** `tools/kernsize.py` priced the resident version at **548
bytes of `.text`** and printed its own alarm — *"the image rung CROSSED: 512
bytes of every machine's RAM, gone"*. That is RAM an IBM PC/XT keeps, forever,
for code it can never execute, on a kernel that #147 had just spent 12KB of
work shrinking. §41.12 had already made this exact argument once, for
`XMEM.DRV`, in almost these words: about 1.2KB of kernel image "reserved
forever on a machine that can never reach it".

**The hibernate hole, which no `[cpu_tier]` gate could have closed.** §87
writes the machine's RAM wholesale and validates magic, build, stamp,
`mem_top` and `vid_kind` on resume — **not the CPU**. Hibernate on a 386,
carry the disk to an 8088 with the same kernel and adapter, resume: the image
restores `cpu_tier = 2` and `vmm_on = 1` over the correct values `cpu_detect`
has just written, and the 8088 executes `push eax`. CLAUDE.md names `xt-mfm`
as the machine to install and hibernate on, so this is a supported flow rather
than a contrived one. `XMEM.DRV` is immune because §87.4 step 1 detaches every
non-disk driver before writing; the plan's island would have been the first
386 code in the **resident kernel**, which is what hibernate restores verbatim.

### 15.1 What was judged, and what it cost

| option | verdict |
|---|---|
| **Resident, `[cpu_tier]`-gated** (the plan's §2) | rejected: 548 bytes and two footprint rungs on every XT, plus the hibernate hole |
| **`XMEM.DRV`'s shape** — `DRVC_OVL`, no row, a resident sniff | rejected: `xm_sniff` is 8086 code and **this probe is `in eax, dx`**. Either ~70 bytes of island stay resident to ask the question, or the image is read speculatively on every 386+ machine (~1 s of boot I/O on a real 386 with no hypervisor) |
| **A full driver** — its own `DRVC_MOUSE` class | rejected: a class is a *publication slot* and nothing needs to find this image by class. `DRVC_MAX` 5 → 6 for nothing, and §58's doctrine forbids reclaiming the retired class 3 |
| **`DRVC_OVL` *with* a `drv_tab` row** | **shipped.** No class, so `DRVC_MAX` stays 5; a row, so `SYSTEM.CFG` carries the request and the Drivers page can withdraw it — which is also the only thing that can decide a question no resident code is able to ask |

The result is **+259 `.text` and +124 `.cold`**, one rung rather than two, and
**no 386 instruction anywhere in `kernel/`**. `kern_small` is byte-identical.

### 15.2 What else the review changed

Each of these was a defect in the resident version, and each is fixed in the
image rather than moved into it:

- **`vmm_p2wake` fed the 8042's acks to `int 09h`.** It wrote `0xD4`/`0xF4`/
  `0xD4`/`0xF5` with IRQ1 unmasked and the keyboard interface live, and read
  the acks testing OBF alone. §9.9.1 step 1 says `mou_p2_init`'s `0xAD` is
  "not tidiness" for precisely this reason — the controller's own replies
  raise IRQ1 — and `kbm_isr`'s aux filter is gated on `[mou_p2]`, which this
  path leaves at 0. A `0xFA` therefore reached the BIOS as scan `0x7A`, and a
  keystroke arriving in the window could be eaten instead. Now bracketed,
  one command at a time, with bit-5-filtered reads.
- **`vmm_bd` ran with `IF` as the caller left it**, citing `xmem.asm` as
  precedent. `xmem.asm` does the opposite: its islands sit inside
  `pushf`/`cli`…`popf` and its header calls that window "the whole
  correctness argument". Now it does too.
- **`vmm_flush` was unbounded**, at attach, inside the boot sequence.
- **The apply ran on the calling task's stack.** §9.10 had just moved that
  chain onto `mou_pstack` so it would stop being a cost every slice pays;
  polling from `task_yield` put it back by another door, measured at
  `task_yield` 28 → 66 bytes and the idle slice within one word of
  `sch_stkdie`. Now `MOUPRIV_ENTER`/`MOUPRIV_LEAVE`.
- **The `vmport=off` mitigation lived inside `$(QEMU)`**, which six recipes
  and four documents tell the reader to replace wholesale. Now `$(QEMUMACH)`.
- And, found only by merging `main` in: **four calls crossed the `.ovlw`
  boundary as near calls** after #147 moved `mouse_init` into the boot
  overlay. `tools/os88ovlchk.py` caught them; nothing else would have.
