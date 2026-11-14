# WireArena: more players, a match finder, and match setup: plan

The owner, 2026-11-14:

> *"Go for more players. For the lobby, I would like both ip and a local
> broadcast to find matches. A central server browser will be a future
> item when os8088.com can host one. When you get to that interface is a
> good time to replace the current 'tons of keys' debug in the opening
> window, with the match setup. Including being able to select the arena,
> bot count, etc."*

This follows [WIREARENA-LINK-PLAN.md](WIREARENA-LINK-PLAN.md), whose
sections 4 (protocol v1) and 12 (Ethernet E1) are what it builds on.
Section 9 records what was built against it.

## 1. Decisions

- **Four participants at most: players and bots together.** WireArena has
  three figure records besides the player (`WA_NFIG`), and every renderer
  table is sized by it. So 4 players and no bots, 3 and 1, or 2 and 2.
  More is the owner's "286+ thing later" (AI-PLAN 7.1), not this plan.
- **The network carrier is UDP**, for both ways of finding a match:
  - a **broadcast** to 255.255.255.255 finds the matches on the local
    network;
  - a typed **IP address** finds one a router away.
  A host using UDP serves both at once. Raw frames (E1) cannot cross a
  router, and a raw claim stops ETHER.DRV's IP stack (SPEC.md 72.22.1), so
  a host on raw frames could not also answer by IP.
- **Raw frames stay, as the fallback for a network with no IP address**
  (no DHCP server, no static address): broadcast only, and "Join by IP"
  greyed with its reason (§47: grey a fact).
- **Packages gain UDP**: three verbs on ETHER.DRV (section 3). The driver
  already has UDP inside (`udp_send`, `udp_in`) for DHCP and DNS. What is
  new is a package port and a small receive queue.
- **The serial cable keeps protocol v1** and two players. Section 5's
  frames are larger, and at 2400 baud v1 already uses ~180 of 240 bytes a
  second.
- **The central server browser is a future item** (os8088.com hosting
  one). Section 6's match list is shaped so that a server's answers could
  be listed beside the broadcast's.

## 2. The match setup window

The opening window's text (every key the game knows, a debug list) is
replaced by the match setup:

```
  WireArena
  Arena     < Heights >        Players   (you)  RIVET-4F2A
  Bots      < 2 >                         COG-91A0
  Skill     < Normal >
  Width     < 40 columns >
  Play      < Alone | Serial | Network >
  Network   < Host | Find | Join IP >  [10.0.2.15    ]
  Matches   HEIGHTS  2/4  RIVET-4F2A   <- found ones, a line each
  [ Start ]  [ Ready ]  [ Keys... ]
```

- **Each setting is a line**: its value between arrows, changed by a
  click on either arrow or by Left/Right on the focused line, with
  Up/Down moving the focus. Enter presses the default button. It is drawn
  with `font_run` (CLAUDE.md: text is `font_run`), row by row, only what
  changed.
- **The buttons are os88ui's** (SPEC.md 13.8): Start (alone: play now;
  hosting: start when everyone is ready), Ready (a joined player), and
  Keys..., which opens the help as a dialog. The key list leaves the
  opening window but not the game.
- **The IP field is os88line's** (SPEC.md 83): the address typed there,
  remembered for next time in `SYSTEM\APPDATA` (SPEC.md 19.9).
- **What a joined player cannot change is greyed with why**: the arena,
  bots and skill are the host's ("The host picks").
- **The Game and Link menus keep their items** as a second way to the
  same settings: the menus still work, and the window shows what they
  set.
- **The width** stays each player's own, as in v1.

## 3. UDP for packages (ETHER.DRV, SPEC.md 72.24 - 72.23 was taken)

| verb | in | out |
|---|---|---|
| `NETV_UDP` 18 | AL = 1 bind CX = the port, 0 release | CF=0; CF=1 NETE_BUSY (one package port at a time) or NETE_NOLINK (no IP yet) |
| `NETV_UDPTX` 19 | SI = the payload, CX = its length (<= 512), DI = 6 bytes: the IP address, then the port | CF=0 sent; CF=1 NETE_NOLINK (no ARP answer yet: try again) |
| `NETV_UDPRX` 20 | DI = a buffer, CX = its capacity, BX = 6 bytes for the source | CF=0 CX = the true length, the source at BX; CF=1 empty |

- **A queue of 4 datagrams of up to 128 bytes** in the driver: about half
  a kilobyte of the driver's bss, which it carries for as long as it is
  loaded, whether or not a port is bound. Section 8 records the bytes.
- **`udp_in` gives the bound port's datagrams to the queue**; a full queue
  drops the newest and counts it.
- **The verbs pump**, as every other verb does: a package that polls
  NETV_UDPRX is the stack's service, inside a bracket too (SPEC.md 53.7's
  rule, for a driver attached before entry).
- **Broadcast** is the destination 255.255.255.255, which `ip_start`
  already sends to the broadcast station address (DHCP does).
- WireArena's port is **30088**.

## 4. Who is who: slots

- **A match has four slots, 0 to 3.** The host is slot 0. Joining players
  take 1, 2, 3 in the order the host accepts them. Bots take the slots
  after the players (2 players and 2 bots: 0, 1 players, 2, 3 bots).
- **A slot is the code on the wire.** That generalises v1's codes, which
  were already host 0, guest 1, bots 2 and 3. The deaths-by-cause table
  is v1's, 4 x 4, unchanged.
- **Here, the local codes are**: 0 us, and figures 1 to 3 for the other
  slots in order, skipping ours. `wa_lkcanon` becomes a table lookup in
  each direction.
- **A figure is ours or remote**: the host owns its player and the bots;
  every other machine owns only its player. `wa_lkremote` keeps its
  meaning (ZF = 1: another machine's).

## 5. Protocol v2 (the network)

Every v2 body starts with the type, the match tag and the sender's slot.
The checks are v1's: one length per type, the CRC.

- **NSNAP** (type 8), each machine, every 3rd sim tick (10 Hz; the
  network has the bandwidth):
  - its player as v1's SNAP (x, y, z / 2, yaw, the host's lift or the
    pitch, the flags);
  - **damage dealt to each slot**: four cumulative bytes;
  - **who dealt it**, from the host (whose bots shoot): four bytes, the
    slot of the last shooter of each;
  - **the last event applied from each slot**: four bytes, the acks;
  - **the host's bots**, at the end: four bytes each (v1's HSNAP form) for
    the bot slots.
  ~32 bytes; 10 Hz from four machines is about 1.3 KB a second on the wire.
- **NSCORE** (type 9): the sender's rows of deaths by cause (its player's;
  the host's bots' too).
- **NEVENT** (type 10): v1's EVENT with the sender's slot. An event is
  dropped from the queue once **every** player's ack has reached it.
- **Sending**: on UDP to each other player's address (up to 3 unicasts);
  on raw frames, one broadcast frame each (every player on the LAN is in
  the match, and a frame of another match's tag is dropped).
- **Hits**: v1's rule, generalised: the shooter judges its hitscan hits on
  its own view and adds to that victim's damage byte. The victim's
  machine applies it, credited to the sender's slot (or, from the host,
  to the slot it names). A hit on a bot goes to the host the same way:
  the bots' damage bytes are the bot slots'.
- **Silence**: a player silent for 55 ticks is LOST to the others; 5 s
  silent and it is dropped from the match, its figure gone. **The host
  silent ends the match for everyone** ("HOST LEFT"). Migrating the host
  is not in this plan.

## 6. Finding a match: the lobby v2

- **MATCH** (type 11, from a host, once a second, broadcast and to anyone
  who asked): the tag, the arena, the bots, the skill, the slots' players
  (identity and ready), and whether it has started.
- **JOIN** (type 12, from a player who picked a match): its identity. The
  host answers with the next MATCH, naming the slot it gave. A full match
  says so.
- **ASK** (type 13): "who is hosting?", sent by Find (broadcast) and by
  Join IP (to the address). A host answers ASK with MATCH.
- The match list holds up to 4 found matches, each with its source
  address, and drops one silent for 5 s.
- **Start**: the host's Start, with every joined player ready, sends
  MATCH with the started flag. Each joined player goes in on it, as v1's
  guest does on the host's tag.
- **Joining a match already playing** is allowed into a free slot: the
  match goes on, and the newcomer's figure appears to the others at its
  first NSNAP.

### 6.1 PEERS: closing the mesh (added while building)

Joined by address, each player knows only the address it typed. A
PEERS frame (type 8) on UDP tells each bound peer the identity and
address of the others, and they HELLO each other directly. Section 9.3
has the details.

## 7. Waves

- [x] **N1 the setup window** (the IP field, its address remembered and the
  match list come with N4, where they have something to do): the lines, the buttons, Keys..., the IP
  field, the settings remembered. Alone and Serial work as now (Serial is
  v1's lobby, shown in the window).
- [x] **N2 UDP for packages** (the verbs; their row is N3's, which runs the
  match over them): SPEC.md 72.24, the three verbs, the queue,
  and a host check. Its row: two QEMUs exchanging datagrams through the
  relay, which also answers DHCP.
- [x] **N3 slots** - built on v1's frames, not v2's (9.3 says why): the
  peer table, slots by identity, every frame to each peer, hits into the
  victim's peer's damage word, acks from every player, LOST and LEFT per
  peer. Over UDP and raw frames; serial is the table with one record.
- [x] **N4 the lobby v2** - as built (9.3, 9.4): found by broadcast and
  by address for up to four (PEERS closes the mesh), quiet peers dropped,
  every peer Ready starts it, HOST LEFT when the host goes. **Dropped by
  the owner (2026-11-14)**: the match LIST - "there are not going to be
  multiple matches on a local network at once" - and more than two on the
  serial cable, a null modem having two ends.
- [ ] **N6 joining a match already playing** - wanted "only if it doesn't
  cost much in performance or size"; 9.4 has the estimate, for the owner.
- [x] **N5 the rows** - `walink4` (four players, then three and a bot, a
  guest and then the host leaving), `walink4ip` (three joined by address),
  `wasetup` (the setup on the glass); the serial, raw and two-player UDP
  rows still pass.

## 8. Costs to record as they land

- The window: its bytes, against the help text it replaces.
- ETHER.DRV: the verbs' bytes and the queue's.
- On the 8088: v2 sends to up to three peers and receives from three.
  MartyPC has no NIC; 86Box (`vm/xt-wire`) is the nearest, not
  cycle-accurate.

## 9. As built

### 9.1 N1: the match setup (2026-11-14)

- **`apps/wirearena/wasetup.inc`**:
  - Five settings, Arena, Bots, Skill, Width and Play (alone, serial
    cable, Ethernet), each a value between `<` and `>`.
  - Up and Down move the focus, which is drawn as an inverted row; Left
    and Right change the value, and so does a click on either half of the
    row.
  - Two os88ui buttons: Play (alone) or Ready / Not ready (linked), and
    Keys.
  - Keys fills the rows with the key list (eleven lines, from the old
    fifteen) and hides the buttons; a key or a click goes back.
- **The rows are walink.inc's**: built as text, compared, and drawn with
  `font_run` only where they changed. The buttons are redrawn only when a
  label changes. The players' rows (YOU and the other) sit under the
  buttons once linked.
- **The window opens on no carrier.** The cable's search starts when Play
  says serial, the card is claimed when it says Ethernet, and both are let
  go when it says alone. Before, the window searched the cable as soon as
  it opened.
- **Bots: 0 to 3 alone, 0 to 2 linked** (four participants). The setup's
  count is `wa_lknb` in every mode now: alone, figures past it stay off
  the board as in a linked match. A bench keeps its three dummies.
- **A joined player's arena, bots and skill are the host's**: the row
  says "(host)", and changing one toasts "The host picks the arena".
- **It fits a CGA's 200 lines**: the window is 160 high at y = 20, the
  buttons under the settings. A first layout put them under fifteen rows
  of text, below the dock, where a CGA could not show them. The font has
  no triangles, hence `<` and `>`.
- **The Game and Link menus keep their items.** The solo rows still start
  from the Game menu, and it starts with the setup's bot count.
- **Found on the way**: the CRC's table was built only when a serial port
  opened. With the window no longer opening one, both machines on
  Ethernet computed CRCs from an empty table and agreed with each other.
  The game accepted its own frames; `walink --net`'s independent parser
  (binascii) did not. The table is built when the window opens now.
- **Registered**: `tests/btnsites.txt` (2 record calls). `tests/btnall.py`
  does not drive WireArena's buttons yet; the link rows drive the setup by
  its keys.


### 9.2 N2 and the first half of N3: UDP, two players (2026-11-14)

The match is found by broadcast and by address; three and four players
are next.

- **The setup's Play row** is alone, serial cable, local network or
  internet.
  - **Local network** asks ETHER.DRV for an address (`ud_open`, retried
    from the window's tick). With an address it binds UDP port 30088.
    With none in ~5 s it falls back to raw frames (E1), LAN only.
  - **Internet** needs an address, and joins the one typed into the
    Address row (row 7: digits, dots, Backspace). On the local network
    that row shows our own address, for the host to give out.
- **`apps/os88udp.inc`** is the carrier: os88raw.inc's twin.
  - HELLO goes to 255.255.255.255 while unbound (or to the typed
    address, joining by address), and to the peer once bound.
  - Everything else goes to the peer's six bytes: its IP and port,
    learned from its HELLO as raw learns a station address. A host on
    UDP is found both ways at once.
- **The relay** in `tests/walink.py` is a hub now and answers DHCP
  (10.7.0.20 up) in the UDP modes. With `--udp-ip` it drops broadcasts to
  port 30088, as a router would. With `--net` it gives no address, so
  the machines fall back to raw.

**The verbs' first caller found three bugs in them, and one in reading
them**:

1. **`NETV_UDPRX` took a pointer in `BX`**, which `OSAPI_DRV_CALL` uses
   for the class and the verb. The source's six bytes come first in the
   buffer now (SPEC.md 72.24 says why).
2. **`eth_pump` saves only CX and BP**, and `NETV_UDPRX` pumped, then
   copied through the caller's DI. It wrote the datagram at an offset in
   the package nobody chose. Found because the package's own counters
   read zero.
3. **The queue's store wrote the source over the slot's length**, so
   every datagram claimed 1,802 bytes and the package skipped it as too
   long.
4. **CF = 0 with AX = `NETE_BUSY` is not success** (ETHER.DRV's
   dispatcher, while its own timers hold the card). A raw consumer
   rarely met it, as the claim stops those timers; a UDP one met it
   constantly. os88udp.inc and os88raw.inc clear AX before every call
   and test it after.

**Checked**:
- `walinkudp` (found by broadcast) and `walinkip` (joined by address,
  broadcasts dropped) run walink's whole match, every datagram valid.
- `walinknet` (no address: raw after ~5 s) and `walink` (serial) still
  pass on the same build.

### 9.3 N3 and half of N4: up to four players (2026-11-14)

**Four machines play one match** - any mix of players and the host's
bots up to four - found by a broadcast on the LAN or by typing one
address.

**v1's frames were enough; v2's were not built.** Section 5 planned
new frame types carrying every slot's damage, attribution and ack in
one broadcast. What was built instead keeps v1's frames and sends each
one to each peer (a unicast mesh, at most three copies):

- **The sender's slot is known from where a frame came from**, so it
  needs no field: each peer has a record (`wa_pt`, three of 32 bytes),
  found by its address, and its slot is fixed at the match's start.
- **Every per-peer quantity lives in that record**: the damage dealt to
  that peer and by whom, the last event applied from it, how far it has
  acked our queue, when it was last heard, LOST, LEFT. v1's code reads
  them through `wa_pc`, a working copy loaded for the frame being taken
  or the peer being sent to, so most of it did not change.
- **What it costs**: on four players a machine sends three SNAPs where a
  broadcast would be one - about 0.6 KB/s each way at 10 Hz, nothing to
  a 10 Mbit card. v2 would matter on raw frames on a busy LAN, or for
  more than four; neither is planned.

**Slots** (`wa_lkslots`, at the match's start): ids ascending, the
lowest is slot 0, the host. Each machine counts the identities below its
own for its own slot, and below each peer's for that peer's. The bots
take the slots after the players: the host's choice clamped to what is
left (four players, none; three, one of two).

**Hits** follow v1's rule per peer: a hit on another player's figure
adds to that peer's damage word, with who dealt it (`WP_ATTR`, a slot).
A guest's hit on a host bot goes into the bot's byte by its place after
the players. The event queue pops an event once every peer still in the
match has acked it. The scoreboard shows the slots the match has, each
player by its identity's name.

**The lobby**:

- **A quiet peer is dropped** (`wa_pdrop`, every beat, 55 ticks). The
  host starts only when every peer bound is READY, so a machine that
  closed WireArena would otherwise block the start for good. Any frame
  from a bound peer counts as heard: one still in a match sends SNAPs,
  not HELLOs.
- **Only a HELLO in the window makes a record.** A stranger's frame in
  a match would make a peer that never acks, and the event queue would
  stop.
- **PEERS** (type 8, 24 bytes with its CRC, UDP only): with every
  HELLO, each machine tells each bound peer the identity and address of
  up to two others it knows. A machine with no record of one makes it -
  an address, no identity - and its HELLOs go there until it binds.
  Without it, two players who each typed the host's address would each
  know only the host.

**Two traps on the way**:

1. **A peer table changes what `wa_pc` means** - the last record
   loaded, and zeroed for the broadcast HELLO. `walink`'s rows read
   peer state through those names and still passed only because one
   peer was loaded last. They read record 0 of the table now.
2. **`tests/wireball.py`'s machines assumed two**: the link port was
   `1 - index`, so a third QEMU asked for serial port -1. It is the
   index's parity now.

**Checked** (one scoped soak, 12 rows, all ok):

- `walink4`: four QEMUs on the hub relay. Each binds the other three;
  slots 0..3 the same everywhere; each player the right figure on the
  other three; slot 3's pistol kills slot 1, its damage in that peer's
  record only, and all four tables agree. One switched off, the other
  three drop it; three players and one bot, synced; a guest's Esc marks
  that peer alone gone. Red with the damage in `wa_pc`, and red with the
  quiet-peer drop off. 46 s.
- `walink4ip`: broadcasts dropped, B and C type A's address; they find
  each other by PEERS and play a match of three. Red with PEERS never
  sent. 32 s.
- `walink`, `walinknet`, `walinkudp`, `walinkip`, and the six MartyPC
  WireArena rows (`walift`, `waroutes`, `wabots`, `wascore`,
  `wabotarms`, `waweapons`): unchanged results.

**Bytes**: `wirearena.bin` 23,563 -> 25,258 (+1,695), bss +105 (the
table and its working copy). The link's cost on the 8088 was not
measured again: on serial the table is one record, and the new work a
tick is that record's copy in and out (two 16-word `rep movsw`).

### 9.4 The setup made of the system's controls; the flash (2026-11-14)

The owner, on the N1 window: *"The whole window is flashing every time I
click or type anything. Please check the interface standards, and use at
least the standard buttons for the buttons, and the text box for the IP
entry (you can hide that line if the mode is not internet)."* And: 40
columns the default on an 8088, 80 on a 286 or better; the Game menu's
"Play X" items gone.

**The flash was measured before it was fixed** (MartyPC, 5150 CGA, the
flicker instrument): one Down key took **33 flashing frames, 617
transient pixels, unsettled after 50 frames**. Two causes:

1. **Every click and key set "draw everything"**: all fourteen rows and
   both buttons, and a button is erased and redrawn by its library (the
   600-pixel transient). The rows already had a cache that draws only a
   changed row; every input defeated it.
2. **Every timer tick took the window's clip** whether or not anything
   would be drawn. Arming a clip spends the pointer's deferred hide
   (SPEC.md 7.1.4), so the pointer over the window blinked about three
   times a second with the window idle. The clip is now taken by the first
   thing that actually draws (`wa_lkclipon`).

After: Down settles in 2 frames with only the pointer lifted while the
row under it is drawn (40 px); with the pointer off the window, **0
transient pixels** (`wasetup`). VGA measured the same.

**Every control is the system's own now**:

- Arena, Columns, Bots, Skill and Play are **drop-downs** (`OS88UI_DROP`,
  SPEC.md 13.14) - banked lists, the Macintosh popup's gesture. A joined
  player's Arena, Bots and Skill are greyed and show the host's.
- The address is **os88line's text field** (SPEC.md 83), right of Play,
  there only while Play is internet. Picking internet with the mouse puts
  the focus in it.
- **Play/Ready and Keys** stay os88ui buttons. Keys - and the system
  menu's About, which WireArena did not answer before - put the key list
  up as the standard **About card** (SPEC.md 12.2, `OS88UI_ABFIT` and
  `OS88UI_ABLEFT` for thirteen lines on a CGA's content box).
- Row 0 says what the link is doing, and on the network our own address
  ("On the network at 10.7.0.20"), for a host to give to players joining
  by address. Rows 1 to 5 are the players.

**Each control draws only when it changed** - its pick, its greying, the
focus marker, the field's presence - against a small cache, as the rows
do. **The painter is opaque by construction**, WireFleet's shape (SPEC.md
114): each element draws its own ground and `wa_sground` fills only the
27 gaps between them, so taking the About card down, or a list that could
not bank, repaints every pixel once with no white first. `wasetup` holds
that: the window pixel-identical after the card and after a list.

**Two bugs the rows caught**: the field took keys only once a timer tick
had given it the focus, so an address typed faster than a tick was lost;
and with the field focused, Space was swallowed as "not a digit" instead
of meaning Ready, as it does everywhere else on the page.

**One found by looking**: with no network card, Internet asked the driver
every tick, and each refusal loaded ETHER.DRV off the disk again - an
hourglass for as long as the window stayed open. A refusal other than "no
address yet" now says so once and stops.

**HOST LEFT**: the host's Esc already told the others (its HELLO says it
is back in the window); the line says HOST LEFT now when the peer that left
is slot 0, and the host's bots leave the board with it.

**Bytes**: `wirearena.bin` 25,258 -> 28,218 (+2,960): the drop-down
(with its bank), the About card, os88line and the setup's own code; the
key list moved from the window to the card. bss 7,104 -> 6,851: the rows'
cache shrank from 14 x 38 to 6 x 40.

**Checked**: `wasetup` (new; red with the old full redraw: 11 frames, 192
transient pixels), `walink`, `walinknet`, `walinkudp`, `walinkip`,
`walink4` (now with HOST LEFT), `walink4ip`, and the six MartyPC
WireArena rows, which start their matches from the setup now (a shared
`start()` in `tests/walift.py`) since the Game menu's Play items went.
Looked at on the 5150 CGA, the XT VGA and the 5150 Hercules.

**Joining a match already playing - the estimate (N6)**. What it needs:

1. the host announcing a running match (a HELLO each beat from inside the
   match - one 20-byte datagram every half second, nothing on serial);
2. a lobby machine showing it and sending Ready to the host;
3. the host taking a HELLO from an unknown address in a match (refused
   today, deliberately) when a slot is free, and answering with a JOIN
   frame: the tag, the settings, the slot it gave, the other peers'
   addresses and slots, the pickups;
4. the others told of the newcomer, and each peer's event sequence
   started where it stands rather than at 1;
5. **slots no longer "players, then bots"**: a free slot is after the
   bots, so the boundary `wa_lknp` stands for becomes a per-slot kind
   (`wa_lkremote`, `wa_lkabsent`, the bot ordinals in HSNAP and GSNAP,
   the scoreboard) - the riskiest part.

Estimated **~1 KB of package** (3-4% of it) and **no measurable cost in
a frame** (one datagram every half second from the host). Not built:
the owner's condition was the cost, so it is theirs to weigh.
