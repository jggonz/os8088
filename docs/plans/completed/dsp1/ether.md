# Driver size pass 1: ether (ETHER.DRV, PCIETHER.DRV)

Agent **ether**, branch `dsp1-ether`, from `de93581a`. Owns `drivers/ether/`,
`drivers/pciether/` and `drivers/wirezone.inc`. `wirezone.inc` was not changed,
and `build/net.drv` is byte-identical to the base. The pass was stopped by the
coordinator's wrap-up. The items queued but not done are under NOT REACHED.

**Read CROSS-FILE item 1 first.** At this tip the fast tier fails one gate,
`drvmem`, and it fails because of what this pass achieved. Both drivers now
fit one 512-byte granule fewer, and the kernel's constants for them sit in
`kernel/driver.inc` and `apps/os88api.inc`. Neither file is this agent's to
edit.

## 1. BASE AND TIP

The image is what RAM pays. The bss is the trailing zeros the file does not
carry. The file is the lz4-packed `.drv`. The claim is the image rounded up to
the heap's 512-byte granule (`drv_load`).

| driver | image | bss | file | claim (granules) |
|---|---:|---:|---:|---:|
| ETHER.DRV base | 17,389 | 480 | 11,106 | 34 (17,408) |
| ETHER.DRV tip | **16,848** | 448 | **10,829** | 33 (16,896) |
| delta | **-541** | -32 | **-277** | -1 |
| PCIETHER.DRV base | 17,076 | 176 | 10,918 | 34 (17,408) |
| PCIETHER.DRV tip | **16,509** | 112 | **10,599** | 33 (16,896) |
| delta | **-567** | -64 | **-319** | -1 |
| OS88NET.COM (netfs's file; inet, tcp, dns, ethsock and ethstate are shared with it) | 14,959 raw | | 10,784 -> **10,572** | |
| NET.DRV (includes wirezone.inc) | 4,185 | 64 | 3,473 | identical |

Total: **-1,108 bytes of image and -596 bytes of file** across the two
drivers, plus **-212 bytes** of OS88NET.COM and -299 bytes of its raw image.

Clusters, from `tools/os88diskuse.py --diff` against the base:

* `os8088-360.img`: ETHER.DRV 11,106 -> 10,829 bytes, 22 -> 22 clusters. The
  free count is unchanged at 37.
* `network360.img` and `util360.img`: OS88NET.COM 10,784 -> 10,572 bytes,
  11 -> 11 clusters.
* PCIETHER.DRV is not on the 360KB system disk (`SYS360OMITDRV`). On the
  other geometries it is 319 bytes smaller.

Each driver is a granule lighter when it is attached: **512 bytes of heap**
on every machine with the card, for the whole session.

## 2. TAKEN

Everything listed was checked by the harnesses in section 2.1. "Hot" means the
per-packet path, the TCP fast path or a path run once per tick. Every hot item
runs fewer instructions and fetches fewer bytes than the base. The measured
counts are in section 2.2.

**inet.inc** (shared by ETHER, PCIETHER and OS88NET):

* `be_get` and `be_put` are gone, 12 bytes. Their callers now load the word
  and swap it with `xchg ah, al`, or compare the field in place against a
  `BE16()` constant. The call sites were in `eth_frame_i`, `udp_in`,
  `udp_send`, `arp_in`, `ip_start`, `ip_finish`, `tcp_out`, `tcp_match`,
  `dns_send` and `dns_in`. Hot, and faster: each one removes a call and a
  return (about 40+ cycles).
* `ip_start`, -28. Hot. The changes:
  - The IP header is written a word at a time: VIHL/TOS and TTL/PROTO are one
    store each.
  - The `IPO_CK` zero was dead and is gone. `ip_finish` zeroes the field
    before it sums, and nothing reads it in between.
  - `mov si, eth_dip`, the second `mov cx, 6` and two `mov di, imm` are
    gone.
  - The ARP-miss exit is shared.
* `ip_pseudo`, -9, plus `eth_ph`, -4 of state. Hot. It was two `ip_ck_part`
  calls over a staged 4-byte tail. It is now six `adc`s in registers. The
  one's-complement sum with an end-around carry is a function of the total
  alone, so it gives the same value. The harness compared 400 random cases,
  including lengths up to 65,535.
* `arp_find`, -15. Hot: it runs on every unicast send. DI walks the table, so
  the six pushes and pops and the shifts per entry are gone, and a `lea`
  finds the hit.
* `arp_put`, -13: a redundant push/pop pair, `and dl, ARP_N-1` for the
  round-robin wrap (with a `%if` that ARP_N is a power of two), and the
  hardware-address slot computed from DI.
* `arp_in`, -14: PTYPE, HLEN/PLEN and OPER are each compared in place.
* `ip_route`, -1. Hot, and about 240 cycles faster: two word compares where
  a byte loop was.
* `icmp_in`, -7. `udp_in`, -4. `udp_send`, -18. `ip_finish`, -3 (hot).
* `eth_frame_i`, -12. Hot: TCP now falls through the dispatch, which is one
  taken branch fewer on the busiest path.
* `eth_pump1`, -2. `eth_ptimers` tail-jumps, -1. Both are hot and faster.

**tcp.inc** (shared by ETHER, PCIETHER and OS88NET):

* `SK_NEXT` (`sub bx, -128`, 3 bytes) walks the socket table where `sk_ptr`
  was called once per slot. It is used in `sk_ring_reset`, `eth_v_gone`,
  `sk_busy`, `sk_reap_kids`, `tcp_lport`, `tcp_match` (hot), `tcp_listener`,
  `tcp_timers` (once per pump), `dns_timer` (once per pump) and
  `eth_v_sktab`. Together with the push/pop pairs this freed, the gains were:
  - `tcp_match` -12, with the ports now held in registers;
  - `tcp_listener` -8;
  - `tcp_lport` -7;
  - `eth_v_gone` -7;
  - `tcp_timers` -9, which no longer banks DX around the retransmit;
  - `sk_busy` -5;
  - `sk_reap_kids` -3;
  - `sk_ring_reset` -3.
* `tcp_out`, -22. Hot. The changes:
  - the ports, the window and the MSS through `xchg`;
  - SUNA/RNXT loaded directly, with SEQ = SUNA + DX;
  - the data offset and the flags written as one word;
  - the CK/URG zeros made through a register;
  - the MSS clamp rewritten as `dec`/`cmp`/`inc`;
  - a redundant push/pop of CX at `.ck`.
* `tcp_seg`, -28. Hot. The in-order test subtracts in place where it used a
  `q_put`/`q_sub` through `eth_ph2`. That write to `eth_ph2` is a dead store:
  `tcp_ackin`, the only reader, writes it first. The SYN-SENT test is one
  `and`/`cmp`. The FIN state switch is reordered.
* `tcp_ackin`, -15. Hot. The SNXT check subtracts in place, and the two
  sign/zero ladders are folded.
* `tcp_in`, -9. Hot: the data offset is `shr 1` twice and `and 3Ch`, and the
  reload of `eth_tlen` is gone because CX still holds it.
* `q_inc` and `q_put1` are new entries (+9). They save 6 bytes at each of
  `tcp_isn`, `tcp_accept_in`, `tcp_seg` and `tcp_kick`.
* `sk_try` -7 and `sk_claim` -7. Both now share one exit, one with
  `CLC_OR_STC` and one with CF already right.
* Smaller items:
  - `sk_ring_geo` -8: shifts replace two `mul`s and DX;
  - `tcp_kick` -6;
  - `tcp_arm` -3;
  - `tcp_finacked` -2;
  - `tcp_rst` -1;
  - `sk_alloc` -3;
  - `sk_reclaim` -2;
  - `sk_ring_get` -1.

**dns.inc**:

* `dhcp_timer` (once per pump) pushes AX only, and `dns_timer` drops DX and DI.
  Nothing below either one writes the registers they were saving: `dhcp_start`
  and `dhcp_request` bank what they spend, and `udp_send` gives AX and BX back.
  The `STKDEPTH-NOSAVE` contract of `eth_pump_i` still holds. I checked it by
  reading every callee. The `stknosave` rows have not been run (see ROWS).
* `dns_qname` -9, with one tail and a `test ax, imm16` skip. `dhcp_opt` -4.
  `ip_parse` -3. `dhcp_request` -6, with a new `dh_emit4`. `dns_send` -2.
  `dns_in` -3. `dhcp_in` -2. `dns_timer`'s scan stores the resolver slot from
  SKO_IDX.

**ethsock.inc, ethdrv.inc, etherui.inc, ethcfg.inc** (cold except where noted):

* `eth_v_open`, -9: SI is never written, so `eth_usi` (2 bytes of state) is
  gone.
* `eth_v_recv`, -7: the crossing test no longer pushes AX or CX. Run once per
  call, and faster.
* `eth_v_rawtx`, -7: one tail, and the frame's path runs the same
  instructions.
* `eth_v_state`, -2. Once per tick, and faster: no `ip_zero` call and no
  `sk_ptr` per slot.
* `eth_v_status`, `eth_v_bulk`: a redundant `stc` each. `eth_v_abort`, -2.
  `eth_v_raw`, -2. `eth_v_sktab`, -4. `eth_pkg`, -1. `eth_dhcp_wait`, -7: one
  `jae` asks for BOUND and FAIL together.
* `eth_draw_btns`, -22: one `.one` helper draws all three buttons, and ZF
  says "greyed".
* `eu_macstr` and `eu_hex` are written at their only call. `eu_hex` was
  unused in PCIETHER.DRV and is gone from it.
* `eu_ip` steps the row itself (-6). `eu_hexb`/`eu_nib` -4. `eth_ipstr` -4.
  `eu_bufnext` -6. `eu_lab` -3. `eth_cp_pane` -4. `eu_ud` -3.
* In the Setup window:
  - `ec_click` reaches `ec_key`'s repaint tail;
  - `ec_repaint` -4, because `ec_paint` never reads the SI it is handed;
  - `ec_paint` -3;
  - `eth_cfg_open` -2;
  - `eth_cfg_load` -3;
  - `eth_cfg_save` -2;
  - `eth_cp_close` falls into `eth_cfg_save` (-4).

**ne2000.inc / nicne2k.inc** (ETHER.DRV):

* `ne_init`, -25: the page registers are held in registers, and AL walks
  PAR0..5 and MAR0..7. Attach and wake only.
* `ne_curr`, -2. Hot: it runs once per receive poll, and it now writes the
  ports directly where it made three `ne_out`/`ne_in` calls.
* `ne_tx_i`, -3. Hot: TPSR, TBCR0/1 and CR are written directly, where four
  `ne_out` calls were.
* `ne_overflow`, -4.
* The spin loops in `ne_probe`, `ne_tx` and `ne_overflow` are deliberately
  untouched. Their iteration counts are their timeouts.

**rtl8139.inc / pcifamily.inc** (PCIETHER.DRV):

* `rt_start` -13, `rt_init` -5 and `rt_halt` -2: each port is a step from the
  last one.
* `rt_rx` -2 and `rt_tx` -2. Both are hot, and faster: a `sub` where a
  `rt_port` call was.
* `rt_claim` -7: `mov word [rt_win], 0` was dead, since a window exists only
  while `[rt_seg]` does, and so was the `clc`.
* `pce_find` -6: `mem_copy` copies the vectors.

### 2.1 How equivalence was shown

Three unicorn harnesses run the **base and tip images side by side** on the
same inputs. They compare every register a routine returns, every named state
variable **by label** (so a moved layout is not a difference), the socket
ring segment, the caller's segment, and the **ordered list of every port read
and write, kernel cell and BIOS call**. Port writes are the frames the card is
handed. The harnesses are in the agent's scratch directory, not in the tree.

* **The stack harness, `harness.py`.** It runs 6 seeds of about 3,100 steps
  each:
  - unit sweeps of `ip_pseudo`, `ip_route`, `arp_find`, `arp_put` and
    `sk_ring_geo`;
  - scripted flows: an active open with data both ways and the peer closing;
    our close through FIN-WAIT-1/2 and TIME-WAIT, or CLOSING; a listener with
    a passive open, NETV_BULK, accept, SKTAB, abort and reaping its children;
    retransmits until it gives up; a DNS lookup with A, CNAME and NXDOMAIN
    answers; DHCP offer, ack and nak; and the doors (busy, gone, nolink, an
    unknown verb);
  - 1,500 random actions: the verbs, raw TX/RX, ARP, ICMP, DHCP, DNS,
    malformed frames, and TCP segments with randomised sequence numbers, acks,
    flags and options.

  Result: **0 differences.** I mutated the tip four ways (the length swap in
  `ip_pseudo`, the TBCR order, the zero-retire test in `tcp_ackin`, and the
  `ip_route` mask) and the harness went red on every one.
* **`neharness.py`.** A register-level 8390 model. It ran 120 card
  configurations: NE2000; NE1000; the PicoMEM's 8-bit PROM over 16KB; a
  floating bus; a stale bus byte; and FE..FE, multicast and random MACs. Each
  one went through `ne_probe`, `ne_init`, `ne_curr`, `ne_tx_i` and
  `ne_overflow` five times, then `ne_stop`. Result: **0 differences**. A
  mutated MAR loop went red.
* **`pciharness.py`.** A fake PCI BIOS and an RTL8139 port model. It ran 5
  seeds. Each covered `pce_find` with CPU-gate, signature, absent-card and
  non-I/O-BAR cases, then `rt_init` with a slow reset, `rt_tx` with TOK, TUN
  and a dead card, `rt_rx` (empty, overrun, a frame, a skip, a bad header),
  `rt_halt` and `rt_stop`. Result: **0 differences**.

The documented-clobber differences that remain are intended. `pce_find`
returns a different DI, which is its documented clobber. `eth_ph2`'s scratch
contents differ, for the dead store named above.

Code the harness did not reach: `ne_rx`'s good-frame path, which is unchanged
apart from `ne_curr`; `sk_reclaim`; the `.busy` spin of `ne_tx_i`, which is
unchanged; and `udp_send`'s ARP-miss exit. These were checked by reading.

### 2.2 Hot-path counts (`hotcount.py`, the same state on both images)

| scenario | instructions | code bytes fetched |
|---|---:|---:|
| `eth_pump_i`, idle, no socket | 328 -> 177 | 678 -> 388 |
| `eth_pump_i`, idle, one ESTABLISHED socket | 376 -> 225 | 798 -> 508 |
| a 1,024-byte in-order segment received, and its ACK sent | 2,293 -> 2,049 | 4,449 -> 3,965 |
| NETV_SEND 1,024 bytes (one segment out) | 4,068 -> 3,729 | 6,744 -> 6,099 |
| a bare ACK received | 423 -> 364 | 897 -> 763 |
| NETV_RECV, 1,024 bytes waiting | 2,397 -> 2,055 | 4,656 -> 4,008 |
| NETV_STATUS | 458 -> 307 | 969 -> 679 |
| NETV_STATE | 540 -> 334 | 1,152 -> 750 |

No hot path gained an instruction. The one place where a single step is
slower is `tcp_timers`' per-slot TS compares, which were not changed. It is
not in the code. No change adds a transfer: the harnesses compare the port
sequence of every transmit, and the sequences are identical.

## 3. REFUSED

* **The spin loops** of `ne_probe`'s `.settle`, `ne_tx`'s `.busy` and
  `ne_overflow`'s `.stopped`, written directly against the port: 6 to 10
  bytes. Refused because a faster turn shortens the timeout each loop's count
  stands for (PERFORMANCE.md rule 3).
* **`eth_v_status`'s success path** through `CLC_OR_STC`: 4 bytes, but +4
  cycles on a verb run once per tick. **`eth_v_rawrx`'s empty path** through a
  shared tail: 7 bytes, but +15 cycles on the poll.
* **`eth_v_accept`'s polled tail** through `.pop`: 3 bytes. Its own comment
  keeps the pops for the per-tick poll.
* **`tcp_kick`'s TS test** as one `mov al` and two `cmp`s: 1 byte, but +2
  cycles on the ESTABLISHED case.
* **`eth_v_send`'s ST test** as `sub`/`cmp`: 2 bytes, but slower for NSK_UP.
* **`ne_init`'s page stores as one word**: 4 bytes. The tip did it first, then
  reverted it. `tests/picomem/nehx.asm` declares `eth_txpg`/`eth_pstart` in an
  order of its own, so the word store would have left `eth_pstart` unset
  there.
* **Storing `dns_id` in wire order** (0 to 2 bytes): it changes which bytes
  the same OSAPI_RAND value puts on the wire.
* **A computed `ec_blk`/`ec_bufs`** in place of the two tables: larger.

## 4. WANTS THE OWNER

None. No wording, behaviour or limit was changed.

## 5. WANTS AN ABI BREAK

None. No `netpkg.inc` value or layout moved. The `tcp.inc`/`inet.inc`/
`dns.inc` constants and the socket record are unchanged. `SKI_*`,
`NETV_*`, `SK_SZ` and the `SKO_*` offsets are as they were.

## 6. CROSS-FILE

1. **`kernel/driver.inc` and `apps/os88api.inc`: the driver-memory column.
   This is what makes `drvmem` fail at this tip.** `t_drvmem` (fast) wants
   `DRVM_IMG_ETH` 34 -> 33 and `DRVM_IMG_PCIETH` 34 -> 33. The PCI one has no
   knock-on: `DRVM_PCIETH` = 77 halves, and the ceiling of 39 KB holds. The
   ETHER one does have a knock-on. `DRVM_ETH` becomes 66 halves, so the
   `%if` at `driver.inc:1822` then needs `DRVM_CEIL_NET` 34 -> 33 in
   `driver.inc` **and** in its SDK mirror in `apps/os88api.inc` (t_mirror).
   That edit moves the SDK fingerprint, so `tools/abilock.py --update`
   (without a bump: a constant clarified, nothing re-laid) is needed too. I
   tried the first half and hit exactly that, then reverted it. All three
   files are off-limits to me. The figure the Control Panel prints is wrong by
   half a KB until this lands, so it is a real correction and not a gate to
   loosen.
2. **OS88NET.COM (netfs).** It shares `inet`, `tcp`, `dns`, `ethsock`,
   `ethusr` and `ethstate`, so it shrank by 212 bytes of file and 299 of raw
   image. It assembles at this tip. Equivalence was shown on ETHER.DRV's
   image, which is the same source. Netfs's base for OS88NET.COM will
   therefore not match a merged tree.
3. **Attach-only code**, for the coordinator's question about giving it back
   after attach:
   - ETHER.DRV holds **332 bytes run only at attach**: `eth_attach`,
     `eth_bases`, `eth_ready`, `eth_cfg_load`, `ec_buf_take`, `ne_memok` and
     its pattern. A further **326 bytes run at attach and at WAKE**:
     `ne_probe` and `ne_init`.
   - PCIETHER.DRV holds **227 bytes run only at attach**, plus **265 run at
     attach and at WAKE**: `pce_find`, `pci_probe`, `pci_bios`, `rt_claim`,
     `rt_init` and `pce_tab`.

   The WAKE share is what a shrink-after-attach scheme cannot give back unless
   the image is reloaded at wake.
4. **apps/os88ui.inc and os88line.inc** are about 1.5KB of each image. The
   largest piece is `os88line_draw` at 204 bytes. `os88ui_band` is 48 zero
   bytes in mid-image. No opt-out knob was found that this driver does not
   already set.

## 7. ROWS TO RUN

The final pass should run these.

* `drvmem` (fast) fails until CROSS-FILE item 1 lands.
* `stknosave` and `stknosave-pciether` check the `dhcp_timer` and
  `dns_timer` saves, `ip_route`, and `ne_tx_i`'s DX.
* `ethernet`, `pciether`, `pcibig`, `ethcfg`, `socktest`, `b4ether`,
  `b4ethswap` and `b4lease`.
* `picomem` (the `nehx` leg runs `ne2000.inc` as it ships), `pmlib`,
  `dospkt`, `dosnetarena`, `romsock`.
* `ftpd`, `ftpdflick`, `ftpdfocus`, `ftpdpix`, `telnet`, `telnetherc`,
  `wirechat`, `wcdrawwired`, `wire`, `thewire`, `wirezone`, `wireflick`.
* `httpmount`, `httpmountname`, `httpinstall`, `httpdrvtool`, `install`,
  `pkgmove-browser`, `pkgmove-ftpd`.
* Any `netbench` or `ETHPROF` row. With ETHPROF on, the checksum stage no
  longer counts the pseudo-header, because `ip_pseudo` stopped calling
  `ip_ck_part`. That is a profile attribution and not a behaviour.

## 8. LOOKED

No boot. The wrap-up came before the QEMU look. `make -j2 mega` is green at the tip. The equivalence evidence is
section 2.1.

## 9. DEFECTS FOUND

* **Stale comments.** Several comments still say four sockets, but
  `NET_SOCKS` is 8. One example is tcp.inc's "Four connections" header. They
  are not fixed and are not a behaviour problem.
* **Dead code: `eu_hex` in PCIETHER.DRV.** It was 29 bytes that nothing
  called. It is now inlined at its one ETHER caller.
* **Dead stores**, now removed: `IPO_CK` in `ip_start`, `[rt_win]` in
  `rt_claim`, `eth_ph2` in `tcp_seg`, and `ec_repaint`'s SI for `ec_paint`.
  None was a bug.

## NOT REACHED

* `tcp_timers`, `tcp_seg` FIN block, `tcp_finacked` and `eth_v_close`: TS
  compared through AL, about 5 + 3 + 3 + 1 bytes.
* `wz_register`/`wz_withdraw` sharing a tail. It is 0 bytes as costed, and
  NET.DRV is affected too.
* Moving the zeroed UI state (`ec_t*`, `ec_f*`, `eu_buf`, `eth_r*`, about 230
  bytes) after `ethstate`. That would change the file only, by an estimated
  10 to 20 bytes of lz4, and RAM not at all.
* The QEMU look-boot, the `make -j2 emu` and `small` variant builds, and a
  PCIETHER mutation test.
