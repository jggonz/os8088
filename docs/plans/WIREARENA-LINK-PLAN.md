# WireArena over a null modem: plan

Two machines, one arena, a serial cable. This is wave 4 of
[MULTIPLAYER-FPS-PLAN.md](MULTIPLAYER-FPS-PLAN.md) section 15 ("the serial
link, with the owner-authority model"), and its section 8.1 and 8.3 are
where the model was first proposed. What is written here is the contract;
section 9 records what was built against it, wave by wave.

## 0. The owner's rulings (2026-11-14)

- **Wave L1 is players only**: two people, no bots. **Wave L2 follows at
  once**: bots owned by one machine (the host). A link match holds at most
  **four participants: two players and two bots**.
- **A lobby screen**, in the window, before the match. It is where match
  setup grows later (modes, Capture the Flag).
- **Keyboard aim is fine** on a machine whose only serial port has the
  mouse on it (the arrows turn and pitch). Better keyboard aiming, or the
  PicoMEM's mouse, is separate work on the roadmap.

## 1. What the OS already gives

- **The serial lease, SPEC.md 115.** `OSAPI_SERIAL` Open/Close/Read/Write,
  the kernel's IRQ3/IRQ4 service feeding two 255-byte rings in the
  package's own pinned region. 2400 baud is divisor 48. The lease survives
  §53 entry, and **Read and Write on a lease opened before entry are legal
  inside the bracket** (§53.7). Open and Close are not on that list, so
  **the port is found and opened in the window, never in the arena**.
- **The mouse is refused for us**: the lease never hands out the winning
  mouse's port (refusal 3), and before the contest settles it decides per
  port (115.1.1).
- **WireFleet's automatic link, SPEC.md 114.9**: both ports searched, the
  lease MOVED between them after a dwell of 40 to 71 ticks drawn afresh, the
  first valid frame from another identity ends the search. Its refusal
  lines say what the user can do about each refusal.
- **WireFleet's framing, SPEC.md 114.6**: 7Eh delimiters, 7Dh escapes (byte
  XOR 20h), CRC-16/CCITT-FALSE. Frames that fail are dropped whole and the
  framer resynchronizes at the next delimiter.
- **Why 2400**: an original 8250 holds one received byte; two 86Box XTs at
  9600 lost ~9% of bytes (SPEC.md 114.3). At 2400 a byte is 4.17 ms.
- **What is missing: a shared link include.** WireFleet's search, framer
  and CRC are `wf_`-prefixed inside `apps/wirefleet/wflink.inc`, written
  around its 64-byte frame. They are written once more here as
  `apps/os88cable.inc` (`lk_` prefix): the search, a framer for frames of
  any length up to 32 bytes, and the CRC. WireArena is its first user.
  Moving WireFleet onto it is story L3.1, so the copy does not stay two.

## 2. The budget: 240 bytes a second each way

2400 baud 8N1 is 240 bytes a second, each way. Each frame costs its body,
two CRC bytes and two delimiters; escapes are rare (a 7Dh or 7Eh in a
coordinate).

| traffic | bytes framed | rate | bytes/s |
|---|---|---|---|
| SNAP: my player | 17 | 10 Hz (every 3rd sim tick) | 170 |
| SCORE: my deaths, and how many by you | 8 | 1 Hz | 8 |
| EVENT: a projectile fired, a pickup taken or rolled | 16 | a few a second at most | ~30 |
| total, in a match | | | ~210 of 240 |

**Hits cost no frames.** The shooter judges a hit on its own view of the
other player (MULTIPLAYER-FPS-PLAN 8.1: "the target you saw is the target
you hit"). It adds the damage to a **cumulative damage word** carried in
every SNAP. The victim applies the difference from the last word it saw. A
lost SNAP loses nothing; the next one carries the total. A chaingun's seven
rounds a second would otherwise be seven frames.

**L2's bots must fit in the ~30 left.** The host decides each bot's moves;
the guest walks the bot along the same route graph itself and is only told
the decisions (a new edge, a target, a shot). Section 8 sizes it. If it does
not fit, SNAP drops to 7.5 Hz while bots are on (every 4th tick: 128
bytes/s, leaving ~80).

## 3. Who owns what

- **Each machine owns its own player**: its position, its health, its
  death and respawn. It is simulated locally with no input delay.
- **The shooter judges its hitscan hits** and sends the damage
  (cumulative). The victim applies it, dies on its own machine, and its
  SNAP says so.
- **Projectiles**: the shooter sends FIRE (kind, place, angle, pitch). Both
  machines fly it. **The victim's machine judges a projectile's hit and
  splash on its own player**; the shooter's machine shows the explosion and
  judges nothing.
- **Kills are counted by the victim**: SCORE carries "my deaths" and "my
  deaths by you". So each machine's YOU and OPPONENT rows agree once a
  SCORE has crossed.
- **Pickups**: whoever takes one sends TAKE(i). Two machines taking the same
  one inside a frame's latency both get it, which is accepted. **The host
  rolls a weapon spawn's kind** and sends ROLL(i, kind). A guest's spawn
  that comes back waits for the host's roll.
- **The host is the lower identity**, WireFleet's rule (114.6): no
  election. In L1 the host chooses the arena and the difficulty; in L2 it
  also owns the bots.

## 4. Protocol v1

Every frame is 7Eh, the escaped body, 7Eh. A body's last two bytes are
CRC-16/CCITT-FALSE (initial FFFFh, polynomial 1021h) over the bytes before
them. **Byte 0 is the type, and each type has exactly one length**: a frame
of another length, or of an unknown type, is dropped before its CRC is
checked. Words are little-endian.

### 4.1 In the lobby

**HELLO** (type 1, 28 bytes since version 2 - 20 in version 1), every 9
ticks while the window is open and the port is leased:

| byte | meaning |
|---|---|
| 0 | 1 |
| 1, 2 | `WA` |
| 3 | protocol version, 2 (the nick added; a version 1 HELLO is dropped) |
| 4..7 | my identity (random, nonzero; lower is the host) |
| 8..11 | the peer I have bound, or 0 |
| 12 | my state: 0 in the lobby, 1 ready, 2 in the match |
| 13 | the arena (the host's choice; a guest echoes the host's) |
| 14 | the difficulty (host's), 15 the bots (L2: 0..2, host's) |
| 16 | the match tag (host's, nonzero once START is sent) |
| 17 | peers I have bound (WIREARENA-NET-PLAN 9.5) |
| 18..25 | my nick: eight characters, space-padded, blank for none (NET-PLAN 9.6; a byte under 32 or over 126 reads as a space) |
| 26, 27 | CRC |

- A HELLO from another identity on our port ends the search (114.9's rule).
- The first such identity is **bound**. While bound, a HELLO from a third
  identity is ignored. One whose bound peer has said "0" for us for 55
  ticks is unbound (the other machine restarted WireArena).
- **START**: once both are ready, the host draws a nonzero match tag and
  sends it in byte 16. A guest that sees its own state "ready" and the host
  ready with a tag sets state 2 and enters the arena. The host enters when
  the guest's HELLO says 2 with the same tag. Both enter within a HELLO
  period of each other.
- **Equal identities** (two machines drew the same) are redrawn, as
  WireFleet does.

### 4.2 In the match

Every in-match frame carries the match tag at byte 1. A frame with another
tag is dropped. That is what keeps a stale peer's frames, after a rematch,
out of the new match.

**SNAP** (type 2, 15 bytes), every 3rd sim tick (10 Hz):

| byte | meaning |
|---|---|
| 0, 1 | 2, the tag |
| 2, 3 | x (`wa_px`) |
| 4, 5 | y (`wa_py`) |
| 6 | feet height / 2 (`wa_feet` + jump, 0..510) |
| 7 | yaw, `wa_ang`'s high byte (256 to the turn) |
| 8 | pitch, -128..127 |
| 9 | flags: bit 0 alive, 1 firing, 2 shielded, 3..5 the weapon held, 6..7 how the sender last died (0 pistol fall, 1 shotgun collapse, 2 bits) |
| 10, 11 | cumulative damage dealt TO the receiver, mod 65536 |
| 12 | the last EVENT sequence applied from the receiver |
| 13, 14 | CRC |

**SCORE** (type 3, 6 bytes), once a second: tag, my deaths, my deaths by
you, then CRC.

**EVENT** (type 4, 14 bytes): tag, sequence (1..255, then 1 again), kind,
eight bytes of payload, CRC.

- **FIRE** (kind 1): projectile kind (grenade 1, rocket 2), x, y (words), z
  / 2, yaw, pitch.
- **TAKE** (kind 2): pickup index.
- **ROLL** (kind 3, from the host): pickup index, kind.

An event is resent every 9 ticks until the receiver's SNAP byte 12 reaches
its sequence. A receiver applies only the sequence one past the last it
applied, so a resend that crosses its ack is not applied twice. Up to 8
events wait, and a ninth waits for room.

### 4.3 Silence

- **55 ticks (1.8 s) without a valid frame** in the match: "LINK LOST" in
  the overlay. The other player stands where last seen, drawn as a still
  figure, and nothing hits them. Frames resuming clear it.
- A HELLO with state 0 or 1 from the peer (it is back in its window):
  "OPPONENT LEFT".
  The figure is gone. The match goes on alone until Esc.
- In the lobby, a peer silent for 55 ticks is shown as gone. It stays bound
  until another identity is heard (114.9's turn-0 rebind).

## 5. The lobby

The window's content becomes the lobby whenever the port is leased; with
no link it shows the help text as now.

```
  WireArena  -  linked on COM2
  You          ready       (host)
  RIVET-4F2A   not ready
  Arena: Heights    Bots: normal
  Link > Ready when you are.
```

- **Link menu**: *Automatic* (the default: the search starts when the
  window opens, as WireFleet's does), *Connect COM1*, *Connect COM2*,
  *Disconnect*, and *Ready* (a toggle). Space or Enter in the window also
  toggles Ready.
- **The host's Game menu chooses** the arena and the bots' difficulty while
  linked. Its Play items no longer start a game; they set the arena, and
  the lobby says so. A guest's arena items set its width and toast "The
  host picks the arena".
- **The width** (40 or 80) stays each player's own. A machine plays the
  width it picked last.
- **The opponent's name** is a robot name from the bots' list plus four hex
  digits of its identity, so two machines' names differ. Entering names is
  later work.
- The lobby redraws only the lines that changed (PERFORMANCE.md rule 1).
  `font_run` draws them (CLAUDE.md: text is `font_run`).

## 6. In the arena

- **The other player is figure 0.** The bots are off in L1: figures 1 and 2
  are parked dead, as the tests park them. The renderer draws figure 0 as
  it draws any figure, so this costs the renderer nothing.
- **Interpolation**: each sim tick, the figure moves a third of the way
  from where it is drawn toward the last SNAP's position, and turns the
  same way. A SNAP every three ticks then lands about when the last one is
  reached. The run animation plays while it moves, the idle animation while
  it stands.
- **Firing**: the firing flag lights the figure's yellow cue (AI-PLAN 4.3,
  `F_FLASH` 86h), so the other player's shots are visible as the bots' are.
- **Its weapon** is not drawn (no figure holds one yet). Its shield flag
  makes its chest yellow, as a shielded bot's is (WEAPONS-PLAN 2.3).
- **Death**: the alive flag falling starts the death animation of the kill
  kind in bits 6..7. The figure lies for 3 s (8.5's rule) and is gone until
  the alive flag rises, at the other player's respawn point.
- **My hit on figure 0** shows the flinch at once (my feedback) but changes
  no health here. The damage goes on the wire.
- **The scoreboard** (Tab) lists YOU and the opponent's name.
- **Esc** returns to the window, where the lobby still holds the link, and
  its next HELLO tells the other machine.

## 7. Cost

- **The link's work in a frame**: drain the RX ring (at 10 fps about 21
  bytes a frame), queue at most one or two frames' bytes into the TX ring,
  and advance the framer. Each byte is one `OSAPI_SERIAL` far call, about
  47 us of call plus the module's bounded service: ~40 calls a frame, a
  few ms on a 4.77 MHz 8088. **L1 measures it on MartyPC** with the lease
  open and the module serving a port with nothing on the cable. The bytes
  a peer would send are counted from the code (QEMU counts work exactly).
- **The renderer pays nothing new**: figure 0 is a figure.
- **Bytes**: the include and the match code in the game half; the lobby
  text and the menus. Recorded per wave against the byte, not the rung.

## 8. L2: the host's bots

*(As drafted. What was built differs, and section 11.2 says how: the bots'
places ride the host's snapshot instead of being walked on the guest.)*

- **Two bots**, figures 1 and 2. They are the host's: the host runs
  `wa_bthink` as now, with **both players as targets**. Figure 0 is the
  guest's player, so the host's bots see and shoot it.
- **The guest does not think for them; it walks them.** Bots walk route
  graph edges at a fixed speed, so an edge and a step on it say where a
  bot is. The host sends **BOT** events on each decision:
  - a new edge (bot, edge, step);
  - a state and target change (bot, state, target);
  - a shot (bot, weapon; its damage to the guest is in the host's
    cumulative damage word, so a hit costs nothing more);
  - a death and a respawn.
  The guest advances each bot along its edge every tick, exactly as the
  host does. A drift is corrected by the next event's step.
- **Engaged bots turn toward their target**. The guest knows both players'
  places, so it turns them itself.
- **Hits on bots**: the guest's hitscan on a bot sends HITBOT (bot,
  damage, kind) as an event, and the host applies it.
- **Estimate**: two bots, about one decision a second each, 16 bytes: ~32
  bytes/s. That is near the margin, so L2 measures it and takes SNAP to
  7.5 Hz if it must.

## 9. Stories

### L1: two players

- [x] **L1.1 `apps/os88cable.inc`**: the lease search (automatic, COM1,
  COM2, disconnect), its refusal lines, a framer for typed fixed-length
  frames up to 32 bytes, the CRC, the RX drain and TX flush. **Verify**:
  `tests/walink.py`'s host parser decodes every frame type the game sends.
- [x] **L1.2 The lobby**: the Link menu, HELLO exchange and binding, host
  by identity, the host's arena and difficulty, Ready, START, the lobby
  text redrawn by line. **Verify**: two QEMUs on a socket pair reach "in
  the match" with the host's arena on both.
- [x] **L1.3 SNAP**: sent every 3 ticks; the other player as figure 0,
  interpolated, running, standing, its firing cue, its shield.
  **Verify**: a player walked on one machine is figure 0 on the other,
  within one SNAP of the place, the same arena.
- [x] **L1.4 Damage, death and score**: the cumulative damage word, death
  by kill kind, respawn, SCORE, the scoreboard with the opponent's name.
  **Verify**: one machine's pistol kills the other's player. Both
  scoreboards then read 1 kill and 1 death on the right rows.
- [x] **L1.5 Events**: FIRE (both fly it, the victim judges it), TAKE,
  ROLL, the resend and the ack. **Verify**: a rocket fired on one
  machine flies on both and kills on the victim's; a pickup taken on one is
  gone on both; Esc on one says OPPONENT LEFT on the other.
- [x] **L1.6 Faults**: garbage bytes, a truncated frame, a pulled cable for
  3 s. **Verify**: no state changes on garbage; LINK LOST, then the match
  resumes.
- [x] **L1.7 Cost**: the link's cycles a frame on MartyPC; the frame rate
  in a linked match against alone.

### L2: the host's bots

- [x] **L2.1 BOT events**: edge, state and target, shot, death and
  respawn, from the host.
- [x] **L2.2 The guest walks them**: the same edges, the same speed, turns
  toward their target; corrected by the next event.
- [x] **L2.3 HITBOT**: the guest's hits on bots, applied by the host.
- [x] **L2.4 Two bots, both players their targets**; the lobby's Bots item
  (0..2).
- [x] **L2.5 The budget measured**; SNAP at 7.5 Hz if it must.

### L3: after

- [ ] **L3.1 WireFleet onto `os88cable.inc`**: its search and framer
  replaced by the shared include, its rows (`wirefleetlink`,
  `wirefleetsystem`, `wirefleetnomouse`) the check.
- [ ] **L3.2 86Box XT pair** (`tools/wireball-pair.py`'s pattern): 2400
  baud between two emulated 4.77 MHz machines, for the byte loss QEMU's
  fast CPU cannot show.
- [ ] **L3.3 kern_mega**: a linked match on kern_mega, beside kern_big.

## 10. How it is verified

- **Two QEMUs on a socket chardev** (`tests/wireball.py`'s owned-QEMU
  bridge, as `tests/wirefleetlink.py` uses it). MartyPC cannot link two
  instances (MULTIPLAYER-FPS-PLAN 8.3), so it is the one instrument for
  real bytes between two OS instances. It checks behaviour and bytes, not
  speed.
- **An independent host parser** (Python's `binascii.crc_hqx`, never the
  game's decoder) reads the bytes on the wire.
- **MartyPC** measures the link's cost in a frame (L1.7). Its numbers are
  for one machine; a pair's timing is the 86Box pair's (L3.2) or the iron's.
- **The rows are named as each story lands**; the soak union runs once
  after the last batch (CLAUDE.md, Testing).

## 11. As built

### 11.1 L1: two players (2026-11-14)

All of L1.1 to L1.6 work between two QEMUs. L1.7, the cost on an 8088, is
taken in the final pass with L2's budget (section 2), when the two numbers
decide the snapshot rate together.

**`apps/os88cable.inc`** is the shared include (L1.1). It is
`os88cable.inc`, not the `os88link.inc` this plan first named: that name
already belongs to the `.LNK` links and autostart include (SPEC.md
26.10.6). Its messages are short enough for a 38-column window row.

**`apps/wirearena/walink.inc`** is WireArena's half:

- **The window**: a one-tick timer runs the search, HELLO every 9 ticks,
  binding and START. Row 0 always says what the link is doing. With a peer
  bound and heard, the help gives way to the lobby. Each row is built as
  text, compared with what was drawn, and drawn only if it changed
  (`font_run`, ground and glyph in one pass).
- **Automatic from the start**, as WireFleet's is: opening the window looks
  for the cable. With no peer, the Game menu plays alone as before.
- **The match**: `wa_lktick` at the head of every sim tick polls the cable
  (24 bytes at most) and sends a SNAP on every third tick, otherwise the
  next waiting event; a SCORE every 30. In a match, figure 0 is the other
  player (`wa_lkfig`). Figures 1 and 2 are held off the board, and the
  bots do not think or hear.
- **The shooter's hits** on figure 0 add to the damage word and flinch
  it. Nothing changes its health here (`wa_hurt`). A rocket at figure 0
  goes off there and kills nothing (`wa_rkhit`). Its splash skips figure 0
  (`wa_boom`). The victim's machine judges both from FIRE.
- **The victim's kill kind** comes from the shooter's held weapon in the
  same SNAP: the shotgun's collapse, else the fall. A splash death is the
  bits. A SNAP's bits 6..7 say how its sender last died, so the other
  machine draws the same death.
- **The score**: each machine counts its own deaths and who caused them.
  SCORE carries "my deaths" and "my deaths by you". So my kills are the
  other's "deaths by you" less my suicides (`wa_lksui`).
- **Pickups**: at the start the host sends a ROLL for every pickup, and
  the guest starts with all of them off. A guest's pickup that comes back
  waits at its last tick for the host's ROLL.
- **No LEAVE event**: the HELLO the window sends again after Esc says the
  same, and a lost one is followed by the next.

**Checked**: `tests/walink.py` (soak row `walink`, ~28 s): every check in
its docstring passes. On the wire, A sent 275 frames and B 294, every one
valid under binascii's CRC; HELLO, SNAP, SCORE and EVENT all crossed.
**Red** with the damage word ignored (A KILL's four checks) and with TAKE
unsent (the pickup's). The rocket still passes then, rightly: splash is
judged from FIRE, not from the word.

**Found on the way**:

- The row's first rocket check wanted the rocket to kill a player at full
  health. A rocket's splash is 128 less the distance (WEAPONS-PLAN), so at
  100 health it left B at 10. The game was right; the check was wrong. B
  now stands at 50.
- `wirearena.bin`'s Makefile rule did not name the two new includes. A
  deliberately broken include then built nothing and tested the old
  binary. They are prerequisites now.
- `tests/dispapps.py`'s symbol map cannot unwrap a parted image whose part
  0 is `OP_ZFILL`. The row uses `tests/walift.py`'s `amap()`, which
  assembles WireArena and holds the result to the build.

**Not yet**: names entered by the players (a robot name and four hex
digits for now), the 86Box pair (L3.2), kern_mega (L3.3), WireFleet on the
shared include (L3.1).

### 11.2 L2: the host's bots, and what the link costs (2026-11-14)

**Built differently from section 8, and simpler.** Section 8 had the guest
walk each bot along the route graph, told only the decisions. That needs
the route walk kept in step on two machines, the lift too (each machine's
lift moves for its own riders). The budget showed it was not needed:

- **The host's SNAP carries its bots** (HSNAP, type 5, 22 bytes + CRC):
  per bot x / 8, y / 8, facing / 4 and a flags byte (alive, firing,
  shield, death's kind). The guest draws them exactly as it draws the
  other player (`wa_lkfig`, a record per figure), their height the floor's
  where they stand.
- **With bots, SNAP drops to 6 Hz** (every 5th tick), the step section 2
  allowed for.
- **The damage word says who dealt it** (HSNAP byte 13): the host's
  player or one of its bots. So a bot's kill of the guest is that bot's.
- **The guest's hits on the bots** ride two cumulative damage bytes in its
  own snapshot (GSNAP, type 6). The host applies them as damage by figure
  0, so a bot shot by the guest turns on it (`wa_bstruck`), and its kill
  is the guest's.
- **FIRE carries the projectile's velocity** (EVENT grew to 13 bytes of
  payload). A bot's rocket is aimed at its target's chest, not along a
  yaw and pitch, so its flight is sent as it left. The guest flies the
  host's bots' rockets, owned there by the same bot. The victim's machine
  still judges every projectile.
- **The score is a table of deaths by cause** (D[dead][cause], the four
  codes on the wire: host 0, guest 1, the bots 2 and 3). Each machine
  counts the deaths it owns: its player's, and on the host its bots'.
  SCORE sends the sender's row, and BSCORE (type 7) the host's two bot
  rows. Each machine builds the board from the table: a death to each, a
  kill to its cause, a kill less for a suicide. L1's "deaths by you" was
  this table's first row.
- **Bots: 0, 1 or 2**, the host's choice on the Link menu (No bots, One
  bot, Two bots; 2 by default), shown in the lobby. The difficulty is the
  host's Game menu choice, as in L1.

**What the link cost, and what was done about it** (MartyPC, the 5150 on
CGA, a host match with two bots faked on one machine: the lease open on
COM2, nothing on the cable, so TX is real and RX is empty):

| | `wa_lktick` a frame | of the frame |
|---|---|---|
| first build: a Write a byte, a bit-at-a-time CRC | 81,681 cycles | 17.4% |
| + SERIAL.DRV's block verbs (SPEC.md 115.1.2), a table CRC | 22,695 | 5.5% |
| + the blocks moved with `rep movsb`, the cable read once a frame | 17,545 | 4.2% |

- **One Write was 2,729 cycles**: the X cell, the module's banking and
  `srl_pump`, for one byte. A 24-byte snapshot was 24 of them, and
  receiving would have been as many again: about a quarter of the frame.
  `OSAPI_SERIAL` gains **Write block (4)** and **Read block (5)**. They
  are module code, so no resident byte is added. An older SERIAL.DRV
  answers them "bad verb", and `os88cable.inc` then falls back to a byte
  a call (the row checks that fallback). A block Write is now 3,500
  cycles for a whole snapshot.
- **The CRC was 400 cycles a byte** bit by bit; with a 512-byte table,
  built into bss when a port opens, it is ~95.
- **An empty Read block is ~2,800 cycles**, so the cable is read on a
  frame's first tick only. The receive ring holds a second of traffic.
- **Receiving is estimated, not measured**: one Read block a frame, ~25
  bytes moved, the framer, and a CRC per frame. That adds ~5,000 to 7,000
  cycles, for a linked frame of about 5.5% on the link. MartyPC cannot
  feed one machine's bytes to another. A pair of 86Box XTs (L3.2) or the
  iron is where both halves can be timed together.

**The budget, counted** (QEMU's bytes on the wire against the game's own
sim ticks, which keep real time on the iron): no bots 191 / 179 bytes a
sim second (host / guest), two bots 162 / 123, of 2400 baud's 240.

**Checked**: `tests/walink.py`, now ~32 s: L1's checks, plus
- the budget in both matches;
- an older SERIAL.DRV (block verbs refused): a byte a call, the other
  machine still sees it move;
- L2: Esc on both and a second match with two bots; the host's bots on
  the guest where the host has them (within a few units); the guest's
  pistol kills the host's bot 1, and both boards agree after BSCORE (the
  guest's kill, bot 1's death); the host's bot 2, engaged on figure 0,
  shoots the guest, hurt on the guest's machine by bot 2.

**Found in the final pass**:

- **Every event was being sent twice.** With bots, an ack's round trip is
  a SNAP period (5 ticks), the bytes on the wire, and the other machine's
  frame, since it reads the cable once a frame. That is more than the 9
  ticks `WL_RESEND` allowed. The two-bot budget read 244 bytes a sim
  second under load. `WL_RESEND` is 20 ticks now (0.67 s); an event's
  resend latency hardly matters. After the fix: 137 to 216.
- **The row's frozen bot walked off.** A flinch held it ~12 ticks, and a
  QEMU sim running fast under load, with every poke stopping the machine,
  let it run its home loop between the guest's shots. The row now stands
  it still: its cached velocity zero for its heading and a leg too long
  to end.

**The final pass** (`os88test.py soak` on the eleven rows this work can
reach: `walift`, `waroutes`, `wabots`, `wascore`, `wabotarms`,
`waweapons`, `walink`, `serlease`, `wirefleetlink`, `wirefleetsystem`,
`wirefleetnomouse`): all eleven pass, `walink` on the re-run after the two
fixes above (three runs in a row).

**Red** with the guest's bot-hit bytes ignored (the bot kill and both
boards) and with the damage word's attribution dropped (hurt by 1, not
by bot 2). Red with the damage word ignored or TAKE unsent, as in L1.

**Not yet** (as L2 landed; 11.3 closed the first two):
- The lift is each machine's own: it rises for its own riders. The other
  player standing on it is not one of them, so the two machines can
  disagree about where it is.
- The bots do not hear the guest's shots: hearing is the host's player's
  alone.
- kern_mega has built SERIAL.DRV's new verbs (`make mega`) but has not
  run them (L3.3).

### 11.3 The guest's shots heard, the host's lift (2026-11-14)

The owner: *"Either sync the bots hearing gunshots or remove that (so we
don't disadvantage the host)."* Synced, and the lift with it.

- **Hearing**: a guest's shot sets a latch (`wa_lkfired`) that its next
  two SNAPs send as the firing flag (two, since one lost would lose the
  shot: the Ethernet row heard 3 of 4 once with one). On the host, a SNAP with that
  flag is a shot heard where the guest stands (`wa_bheara`, the hearing
  routine given the shooter's place and code). The host's roaming bots
  within `WA_HEAR` hunt the guest, as they hunt the host's player.
  - **The latch is the point**: the muzzle flash lasts 2 ticks and a SNAP
    comes every 5, so the flash alone would carry about 2 shots in 5.
  - The row checks four separate shots, each heard by a bot that cannot
    see the guest. With the latch removed it heard **1 of 4** (red); a
    first broken run heard 4 of 4 by luck, which is why it is four shots
    and not one. Without any shot: 0 of 4.
- **The lift is the host's**: the guest standing on it is a rider there
  (`wa_lkonlift`: figure 0 inside the lift's box and within 40 units of
  its top; 8 units' slack stalled the climb, since its height comes a
  SNAP late). The host's SNAP byte 8 carries the lift's height / 2 in
  place of the pitch, which no receiver reads. The guest's lift follows
  that height at up to twice its speed. The row stands the guest on it:
  the host's lift rises to the top, and the guest's follows and carries
  the guest up. **Red** with the rider check removed.
- **The budget**, counted against each sender's own sim ticks now (the
  two machines' sims run apart under QEMU on a loaded host, and one
  machine's clock had been used for both): no bots ~180 / 180 bytes a sim
  second, two bots ~180 / 125.

## 12. Ethernet (wave E1, 2026-11-14)

The owner: *"Then continue to ethernet."* MULTIPLAYER-FPS-PLAN 8.2 is the
proposal; this is what E1 builds on it.

### 12.1 What E1 is, and what it is not

- **The same match over the card.** Two players, the host's bots, every
  frame of section 4 unchanged: the bodies, their types and lengths, and
  their CRC. Only the carrier differs. The CRC is redundant on Ethernet,
  whose FCS covers the frame, but keeping it means one validator for both
  carriers (`lk_frame`, factored out of `os88cable.inc`'s framer).
- **Not yet three or four players.** That is E2. Section 4's protocol is a
  host and a guest throughout: one damage word, one remote player, the
  codes 0 to 3. Four players need the protocol generalised, and E1 does
  not pretend otherwise.

### 12.2 The carrier

- **`ETHER.DRV`'s raw verbs** (SPEC.md 72.22): `NETV_RAW` claims the card
  and answers our station address; `NETV_RAWTX` sends a frame;
  `NETV_RAWRX` drains one. The claim is exclusive and stops the driver's
  own pump (DHCP, TCP) while it is held. So it is taken when the user
  picks Link > Ethernet and released at Disconnect or close, never held by
  a window that is not linking.
- **The driver is attached on demand**: `OSAPI_DRV_USE` with `DRVU_UP`
  loads `ETHER.DRV` off the system disk if it is not up. Nothing is
  written to `SYSTEM.CFG`.
- **A frame**: destination, source, EtherType 88B5h (IEEE 802's local
  experimental type: never forwarded off a LAN as anything else's), then
  one byte giving the body's length, the body with its CRC, and padding
  to the 60-byte minimum. The length byte is the one addition: a padded
  frame does not say where the body ends.
- **HELLO is broadcast; everything else goes to the peer's station
  address**, learned from the source of its HELLO when it is bound.
- **`apps/os88raw.inc`** (`rw_` prefix) is that carrier, written once:
  `rw_open` (attach, claim, our address), `rw_close`, `rw_send` (a body to
  a station address) and `rw_poll` (drain up to N frames, the includer's
  `rw_take` called with each body of our EtherType and its source).
- **In the bracket**: SPEC.md 53.7 now lists `OSAPI_DRV_CALL` to a driver
  attached before entry, for a verb that reads no disk. This closes the
  gap MULTIPLAYER-FPS-PLAN 8.2 named. The card is polled, so the game's
  drain is all the service it gets: once a frame, as the cable's read is.

### 12.3 The lobby

Link > **Ethernet** (or E in the window) leaves the serial port and
claims the card. Row 0 says "Looking on Ethernet..." and then "Linked on
Ethernet". Everything after binding is section 4.1's. Link > Automatic
returns to the cable; Disconnect lets both go.

### 12.4 How it is checked

`tests/walink.py --net`: the same row, the two QEMUs on `ne2k_isa` cards
whose `-netdev socket` ends are a Python relay. The relay is the cable's
analogue: it counts, it can be pulled, and it can inject a garbage frame.
QEMU is the instrument by the closed list (CLAUDE.md, entry 5: MartyPC has
no NIC).

### 12.5 E1 as built

Built as 12.1 to 12.4 say:

- **`apps/os88raw.inc`** carries the bodies. `lk_frame`, the validator
  factored out of `os88cable.inc`'s framer, checks them for both carriers.
  `lk_seal` appends the CRC for a carrier that sends whole bodies.
- **WireArena switches carriers in one place each**: `wa_lksend`,
  `wa_lkpoll` and `wa_lkup`. Link > Ethernet (or E) claims the card;
  Automatic, COM1, COM2 and Disconnect give it back. Closing the window
  gives back both.
- **HELLO goes to everyone; the rest goes to the peer**, whose station
  address `wa_lkhtake` records from each HELLO of its bound peer.

**Checked**: `tests/walink.py --net`, soak row `walinknet` (~35 s). The
whole of `walink`'s match passes on two QEMUs' `ne2k_isa` cards behind a
Python relay; the checks that are the cable's own (the budget, the older
SERIAL.DRV) are skipped there. On the wire A sent 386 frames and B 421,
every one valid. **Red** with the peer's station address never recorded:
the HELLOs cross, so both bind, and nothing after them arrives. The serial
`walink` still passes beside it.

**What the card costs is not measured**: MartyPC has no NIC.
MULTIPLAYER-FPS-PLAN 8.2's figure is ~1 to 1.6 ms a verb on a 5150. A
linked frame drains what came (about one frame at 10 fps) plus the empty
answer, and sends a third of a SNAP's frame, so about 2.5 verbs: an
estimate of 3 to 4 ms, near 4% of a frame. `vm/xt-wire` (86Box, an NE1000
in an XT) or the iron is where it can be timed, and 86Box is not
cycle-accurate.

**Next, E2**: three and four players. The protocol stops being a host and
a guest: a damage word per pair, a remote player per peer, codes past 3.
With players and bots together, a match of more than four figures is
also more than WireArena's three figure records.
