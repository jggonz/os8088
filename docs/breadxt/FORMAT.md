# BBX1 source format and editable examples

SPEC.md section 130 is binding. The guest codec is `apps/breadxt/file.c`; the document
validator and sole pin accessor are in `model.c`. A project contains source
geometry, objects, parameters and probe/view bindings. It contains no solved
net numbers, cached voltages, capacitor trajectory or canned waveform.

All multi-byte fields are unsigned 16-bit little endian. Maximum size is 3,120 bytes:
48 header bytes plus 16 bytes per part/wire, at most 64 parts and 128 wires. Exact
length must match counts. No trailing data, unsupported version/profile,
nonzero reserved fields, invalid IDs, footprints, occupancy or parameters are
accepted. Structural validity does not imply a circuit solves: unfinished and
intentionally broken circuits are valid source documents.

| Byte offset | Field |
|---|---|
|0 | ASCII `BBX1`, four bytes |
|4 | Version 1 |
|6 | Header length 48 |
|8,10 | Part count, wire count |
|12 | Next stable ID; 1..65534 allocate, 65535 means exhausted |
|14,16 | Probe A/B hole; 65535 when unbound or bound by part identity |
|18,20 | Pan column 0..29, pan row 0..13 |
|22 | Zoom 1 or 2 |
|24 | Step in 100 us units, 1..1000; default 10 |
|26 | Time/division in 100 us units, 10..60000; default 5000 |
|28 | Trigger 0 free / 1 rising / 2 falling, channel A |
|30 | Board profile 1 |
|32,34 | Probe A/B kind 0 hole / 1 part pin / 2 branch |
|36,38 | Probe A/B stable part ID, zero for hole mode |
|40,42 | Probe A/B pin, 1-based; branch reference pin 1; zero in hole mode |
|44,46 | Reserved zero words |

Profile 1 is one 30-column board: physical hole `row*30+column`, zero-based.
Rows 0/1 are upper +/- rails, 2..6 are a..e, 7..11 are f..j, 12/13 are lower
+/- rails. Each column has one a-e strip and a separate f-j strip. Each rail
breaks between columns 15/16. The explicit profile persists these dimensions
and rail metadata; unknown profiles are refused rather than guessed.

A part record is eight words: `id,type,pin[0],pin[1],pin[2],value,param,flags`.
Type IDs are 1 resistor, 2 LED, 3 switch, 4 supply, 5 ground, 6 pot, 7 input,
8 indicator, 9 clock, 10 HC00, 11 HC04, 12 HC08, 13 HC32, 14 HC86,
15 capacitor and 16 NE555. Unused stored pins are 65535. For DIP parts only pin[0] is stored: it anchors pin 1 on
row f with notch left; the computed physical map spans e/f. Flag bit 0 rotates
180 degrees; switch-only bit 1 means closed. Two-lead rotation exchanges their
physical destinations while preserving pin identity; pot exchanges ends 1/2.

Resistor/pot values are 10-ohm units, 10..65535. Cap values are nF, 1..60000;
its parameter is initial mV, 0..6000. Supply is 4500..6000 mV. Pot parameter is
wiper 0..1000 permille. Input parameter 0/1/2/3 means 0/1/X/Z. Clock value is
period 2..60000 in 100 us units, duty parameter 1..999 permille. All other unused
value/parameter fields are zero. LED pin 1 is anode and pin 2 cathode; capacitor
pin 1 is positive and pin 2 negative. Pot pins 1/2 are ends and pin 3 the wiper;
supply pin 1 is positive and pin 2 return. Ground/input/indicator/clock use one pin.

A wire record is seven words and two bytes: `id,a,b,bend[0..3],color,nbend`.
Each bend is a zero-based (column,row) pair. Color 0..15, bend count 0..2. Unused
bend words are zero. Segments are orthogonal and nonzero; endpoint pairs may
not duplicate another wire. A self-connection at one hole is refused. Distinct
holes on the same internal strip may be redundantly connected. Bends and
crossings have no electrical effect. Only the actual inserted endpoints do.

Stable IDs 1..65534 are unique across both object kinds; next ID exceeds every
live ID or is 65535. Probes to part pins/branches reference these IDs, not array
indexes or compiled nets. Deletion unbinds its probes; Undo restores them.
Hole probes have zero ID/pin. Unbound is kind 0, hole 65535, ID 0, pin 0.

`bb_decode` never accepts the live `bb_doc` as output. It parses bounded staged
storage and invokes the same structural validator as editor transactions.
Failure may replace staged scratch and last-operation diagnostic, but leaves
live source, undo, dirty state, running state, published result and scope
untouched. The caller commits a successful staged document. `bb_encode`
validates before writing. Whole-file saves use the native allocate-new-chain,
commit-directory, free-old-chain replacement service; no delete-first dance.

## Value input grammar

The guest parses decimal digits, optionally one decimal point and at most three
fractional digits, surrounding spaces and one exact supported suffix. It rejects
signs, exponents, unknown suffixes, overflow and precision finer than the encoded
unit. It shifts decimal digits before 16-bit accumulation, so `655350ohm` and
`655.35k` are both representable as 65535 encoded units without a wide C integer.

| Type | Suffixes and bare-number unit | Examples |
|---|---|---|
| Resistor/pot | bare/ohm/Ohm/R=ohms;k/K/kohm/kOhm=kohms;M/Mohm/MOhm=Mohms |330,330ohm,0.33k,10k,68k,655.35k |
| Capacitor | bare/n/nF=nF;u/uF=uF |10nF,10uF,0.001uF,60uF |
| Supply | bare/mV=mV;V=volts |5000,5000mV,5V,4.5V |
| Clock | bare=100us units;us/ms/s=time units |200us,1ms,1s |

333ohm is rejected because resistance uses exact 10-ohm increments; it is never
silently rounded. A 100-ohm / 6 V network can be structurally valid but exceed the
solver's 30 mA branch-current envelope and diagnose when run.

## Original examples and independent logical graphs

The generator is `examples.c`, compiled into the application and into
`hosttest/export.c`. It creates source objects only, then calls the shipping
validator. The exporter serializes through the shipping codec, decodes and
serializes again, compares exact bytes and checks the following logical graph
relationships against the physical net extraction. Electrical expected values
are independent calculations; they are not used to animate or drive a circuit.

| Document | Parts/wires/nets; bytes | Graph and expected result |
|---|---|---|
| LEDTEST.BBX |4 / 0 / 3; 112 |5 V -> 330 ohms -> LED -> GND. The 2 V + 10 ohms model gives 3/340 A = 8.823529 mA. Reverse blocks ideally. |
| DIVIDER.BBX |4 / 0 / 3; 112 |5 V -> 10k -> midpoint -> 10k -> GND. 2.500 V, 250 uA series current. |
| POT.BBX |3 / 0 / 3; 96 |10k track across 5 V/GND, independent midpoint wiper at 500 permille. 2.500 V; endpoints are ideal zero-length segments. |
| SWITCH.BBX |5 / 0 / 3 closed; 128 |5 V -> 330 ohms -> closed switch -> LED -> GND. Open stops current. |
| LOGIC.BBX |6 / 5 / 14; 224 |HC00N actual pin 14=5 V, pin 7=GND, pins 1/2=HIGH, pin 3=indicator. First NAND output LOW; other unused inputs Z/X. The inputs (g5, g9) and the indicator (g22) are wired to pins 1, 2 and 3 rather than standing in g12-g14, where they drew over the chip and each other. |
| BROKEN.BBX |3 / 0 / 2; 96 |LED directly across 5 V/GND. Expected missing-series-resistor diagnostic; source still saves/reopens. |
| RC.BBX |4 / 0 / 3; 112 |5 V -> 10k -> C positive; 10uF C negative to GND, initial 0 V. Tau 0.1 s, continuous 3.160603 V at 0.1 s; 1 ms BE about 3.151444 V. |
| FLASHER.BBX |9 / 10 / 7; 352 |Verified eight-pin astable below. |

FLASHER part IDs: 1 supply, 2 ground, 3 NE555, 4 RA 10k, 5 RB 68k,
6 timing C 10uF, 7 CONT bypass 10nF, 8 LED resistor 330 ohms, 9 LED.
Wire IDs are 10..19. Chip pins are 1 f12, 2 f13, 3 f14, 4 f15, 5 e15,
6 e14, 7 e13, 8 e12 (one-based hole labels). Pins 8/4 and RA.1 are VCC. Pin 1, Ctiming.2, Cbypass.2, LED.2 and supply return are ground.
Pin 7, RA.2, RB.1 are discharge. Pins 2/6, RB.2, Ctiming.1 are timing. Pin 5 and
Cbypass.1 are control. Pin 3, RLED.1 are output; RLED.2, LED.1 are LED anode.
The timing capacitor is positive toward 2/6. Wire 11 explicitly bridges the
bottom negative rail break; no implicit rail continuity is assumed.

Independent settled ideal estimates: high `ln(2)*(10k+68k)*10uF = 0.5406548 s`,
low `ln(2)*68k*10uF = 0.4713401 s`, frequency 0.9881473 Hz, duty 53.4246575%.
The first high interval charges from 0 V and is longer: exclude it from settled
measurements. Changing RB, reset, power or a timing wire must alter operation.
Probe A binds stable ID 3, pin 3; probe B binds stable ID 6, pin 1. RC probes bind
its capacitor and supply pins. No special filename recognition enters the solver.

## Rebuild and inspect

The on-demand `breadxtdisk` build invokes the host exporter. It uses no upstream
source, simulator or numeric adapters. Standalone command proven during module
implementation:

```
cc -std=c99 -O2 -DBB_HOST -I apps/breadxt apps/breadxt/hosttest/export.c -o /tmp/breadxt-export
/tmp/breadxt-export /tmp/existing-output-directory
python3 tools/breadxtbbx.py inspect /tmp/existing-output-directory/FLASHER.BBX
python3 tools/breadxtbbx.py to-json FLASHER.BBX -o FLASHER.json
python3 tools/breadxtbbx.py from-json FLASHER.json -o FLASHER.BBX
```

The Python tool is an independent bounded structural reader/converter, not an
electrical solver and not a replacement guest dialect. Its JSON contains every
source field. All eight generated examples roundtrip byte-for-byte through it.
The installed guest decoder remains the distribution authority.

## Electrical interpretation limits

Exactly one ground-referenced 4.5–6 V supply is supported by the simulator.
Structural validation still permits saving an unfinished or deliberately broken
source. A second supply receives an electrical one-supply diagnostic.

Current uses pin 1 to pin 2 for every supported two-terminal part. Supplying
current is negative at a source whose positive terminal is pin 1; capacitor
backdrive into that terminal is positive. The 30 mA bound includes the source's
total load. Multi-terminal pot/IC current is N/A.

An active all-LED ideal-rail path requires a real series limiter; a parallel
resistor does not satisfy that condition. At near-zero current, mutually
anchoring LED-only cycles receive a conservative FLOAT diagnostic instead of
an arbitrary clamp. This explicitly bounded educational model does not claim
that every refused physical circuit is insoluble. Real R/C references and
stored capacitor conditions are retained. These are simulator policies rather
than additional fields in BBX1.
