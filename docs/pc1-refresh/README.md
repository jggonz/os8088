# PC1 refresh control — screenshot evidence

Captured from the running compact Control Panel on MartyPC, CGA 640x200.

- `cga-non-pc1-display.png`: ordinary non-PC1 guest. Display was temporarily
  unhidden by the test to inspect it: the normal caption is present, with no
  PC1 controls.
- `cga-pc1-fixture-60hz.png`: **synthetic PC1 identity fixture**, produced by
  replacing only the loaded panel's identity probe with CLC/RET. The actual
  shipped UI handles the clicks and saves 60 Hz. This proves layout and
  interaction, not physical PC1 detection or the Yamaha output frequency.

Reproduce: `python3 tests/pc1refresh.py --shots build/pc1-evidence`.
See `docs/plans/PC1-REFRESH-PLAN.md` for protocol tests and pending field checks.
