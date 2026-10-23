# Browser 1.6c combined field test

This procedure validates the current Browser package, its embedded lazy
Download Manager painter and selection-copy code, the 1.6c HTML fixtures, and
the existing RAM-to-EMS arena behavior together. EMS acceptance must be run by
the user on the 8088/86Box setup with the EMS hardware and driver available;
QEMU results in this checkout do not count as EMS validation.

## Prepared artifacts

- `build/br16c-check360.img`: 360 KB Browser/fixtures disk for B:, compatible
  with a 360 KB floppy drive. It contains the current Browser package and the
  compact local HTML fixtures; the large memory fixture is fetched over the
  active Ethernet connection.
- `build/br16c-check.img`: 1.44 MB Browser/fixtures disk for B:. It contains
  the Browser package built from `browser-1.6c-classic-html`, `BR16C.HTM`,
  `DEMO.HTM`, `FORMAT.HTM`, `TORTURE.HTM`, `FRAMESET.HTM`, and the
  768 KiB `EMSGROW.HTM` visible-content fixture.
- Host test server, `http://192.168.0.106:8091/`: its landing page links to the
  768 KiB network fixture at `/emsgrowth.htm`, so the HTTP receive/parser and
  document-growth path are exercised. In an environment routed through SLiRP,
  use its host alias `10.0.2.2`; a bridged 86Box VM should use the host LAN IP.
- `build/browser.o88` is the exact Browser package placed on B: and copied to
  the test server as `BR16C.O88`. It contains its lazy parts internally; no
  separate painter or clipboard module files are needed on the guest disk.

Build both app disks with `make browsertestems`. Prefer `br16c-check360.img`
when the VM's B: drive is configured as 360 KB. The test fixture generator is
`tools/brmemory.py`; it emits exactly 786,432 bytes and places
`END-EMS-FORMATTED-GROWTH` after the visible rows.

## Test sequence

1. Keep the same OS boot disk and conventional-RAM size for both runs. Confirm
   the Ethernet driver is active before opening the Browser. First boot with
   EMS disabled, open Browser from B:, then visit
   `http://192.168.0.106:8091/emsgrowth.htm`. Record the last visible
   `EMS-ROW-xxxxxx`, whether `END-EMS-FORMATTED-GROWTH` appears, and the exact
   truncation/error text. Do not infer failure merely because the full source
   body was received; this fixture expands into formatted text and line data.
2. Restart the same VM with its EMS board/driver enabled, leaving conventional
   RAM, network card, Browser floppy and URL unchanged. Reopen and reload the
   same page. Success is the final `END-EMS-FORMATTED-GROWTH` marker with no
   truncation and no system lock. Note the final visible row, any EMS count the
   OS exposes, and whether other apps remain responsive. Do not treat the
   allocated amount as an error if the OS reports it separately from free RAM.
3. From the server index, open the root test page and click the local fixture
   links. On `DEMO.HTM`, drag-select `os8088`, verify Ctrl+C and the one-item
   right-click Copy command each place exactly `os8088` in the shared
   clipboard. This now loads the selection-copy routine from the embedded
   lazy part on demand.
4. Open `FRAMESET.HTM`; check a named-target link changes only the intended
   child while the sibling frame remains visible. Open `FORMAT.HTM` and
   `TORTURE.HTM` to check entities, CR/LF handling and the recovery cases
   represented by those fixtures.
5. Click `/download-test.zip` in the server index. Confirm the Download
   Manager opens on demand, its animation and progress remain clean, speed and
   ETA display numbers, and completion offers Exit. Confirm the destination
   file has 8,482 bytes. For a separate cancellation check, cancel a second
   transfer before completion and confirm the partial file is removed and the
   manager closes without leaving a busy state.

Record a screenshot of the final row with EMS disabled and enabled, plus one
Download Manager active or completed screenshot. If an EMS-only failure
occurs, preserve the exact page URL, row marker, error text, memory/EMS driver
configuration and whether the desktop stayed responsive.

## Previous physical result and follow-up

Latest field report, 2026-10-22: with EMS enabled, the user opened
`http://192.168.0.106:8091/ems-pressure.htm` by following the link on the host
server's index. The OS froze during the test, although the display did not show
the earlier graphics corruption. The last visible row, elapsed time, and
whether the machine later recovered were not reported. This is a failed
responsiveness result; it does not validate the source-spill change. The
fixture is 307,200 bytes with roughly 3,800 visible PRE rows. The likely
blocking cause was `br_finish` running `br_measure`/`br_layout` while holding
the global graphics lock. The worker now lays out without that lock and yields
every 4 KiB; paint and page-input callbacks avoid the line table while it is
being rebuilt. The rebuilt disk needs the same network-based physical retest;
this code change is not yet field-validated.

The user reports that the no-EMS run reached `EMS-ROW-000306`, showed no
truncation, and released RAM after opening. With a 2 MiB EMS configuration,
the run reached `EMS-ROW-000395` and reported Page truncated. The incremental
parser retains the complete raw HTTP source in conventional memory for View
Source/Save As; only the formatted-document and line-index arenas had EMS
backing. That run identified the missing source-storage path, but did not
establish an EMS-driver failure. The source now migrates to EMS when the
conventional reserve refuses growth. Save As and event-time lookups use
bounded EMS windows. The parser stages source slices of at most 512 bytes in
the existing receive buffer; formatted-document EMS mappings can no longer
replace bytes underneath its active parser cursor. The supplied screenshot
showed broad display corruption on the preceding direct-map build, consistent
with this shared-frame hazard; the new behavior still needs a physical retest.
Repeat the 2 MiB run against the rebuilt Browser; the
acceptance marker remains `END-EMS-FORMATTED-GROWTH`. QEMU tests in this
checkout do not validate the EMS path.

## Source-spill implementation checks

- Browser image: 47,781 bytes; package BSS: 13,654 bytes (61,435 combined,
  5 bytes below the 61,440-byte package ceiling). To fund lock-free layout,
  the visible form-control cap is 22 (previously 24); the page arenas and EMS
  policy are unchanged.
- `make browsertest`, `tests/brtest.py`, and `tests/br14.py` pass. The
  `brcapacity.py` guest did not start because QEMU could not bind its QMP socket
  on the WSL-mounted checkout path; no capacity result is claimed from this
  run.
- `make browsertestems` rebuilds `build/br16c-check.img` and
  `build/br16c-check360.img` with the current Browser. `EMSGROW.HTM` is served
  over Ethernet for the RAM/EMS comparison.
- `tests/ethernet.py --expect-incremental` did not complete in this WSL/QEMU
  run after opening the Browser; no result is claimed for it.
- No automated or physical result here validates EMS allocation, migration,
  mapping, or release. The user's 86Box/physical retest is required.

## Known automated coverage and limits

On the host build, `brtest.py` and `br14.py` pass. The Download Manager's
70,003-byte known-length test passes, observes animation frames 0..7, checks
monotonic 0..100 percent across the 64 KiB counter rollover, verifies the
exact output bytes and captures active/completed screens. The full `brclick.py`
interaction test previously stalled in its QMP mouse-driving phase in this
environment, so its selection-copy scenario remains part of the manual field
check above. No result here validates EMS. The existing 1.6c limits described
in `BROWSER-1.6C-PLAN.md` remain in force.
