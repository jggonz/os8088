# Weave & Loom

Weave runs web-style applications on the 8088 by inverting the browser: the
components are native, and the app's markup (WML), script (WJS) and formulas
(FX) are compiled at pack time into one `.WAB` bundle the runtime interprets
as a display list plus event-handler bytecode. Loom is the in-OS IDE that
edits the sources and packs the bundle on the machine, byte-identical to the
host packer. The binding contract for every byte, opcode and refusal is
[docs/WEAVE-SPEC.md](../../docs/WEAVE-SPEC.md); `tools/weavesim.py` is the
host reference implementation, and `demos/` holds the three committed demo
projects (FORM, SHEET, PONG) it packs.
