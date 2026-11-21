# Borat Teaches Typing

Binding package contract for BORATYPE.O88, SPEC.md §134. This describes the
intended implementation; the checked plan and evidence distinguish what has
been built and observed. The supplied build prompt and nine images define
the product scope. No external publication is authorized by this task.

## 1. Platform and ownership

Native C, one translation unit, SmallerC → cc8086 → NASM → CC_PACKAGE.
The package declares `OS88_HIGH | OS88_HIGHMEM` on kern_mega: its region
and small data claims may use opaque selectors above1MB. The eager part
carve remains conventional; the renderer never performs paragraph arithmetic
on its own segment or band, and no BIOS/DMA receives an application claim.
Default FSX entry remains protected when required by these claims.
No kernel resident bytes or new API slots. The C restrictions in SPEC.md §73
apply. A package instance owns one window and its session. Four local profiles
share a bounded versioned store on the package volume's SYSTEM/APPDATA.
Mouse and keyboard profile selection both reset the name-edit state: the
first printable character replaces that selected profile's prior name.
Only one writer may own the store at a time; another instance must display
its read-only state. UI callbacks own all state; face rendering is read-only (the outer paint
callback may detect geometry change and pause before drawing), and no
worker opens files. A first printable attempt starts lesson timing.

## 2. Session and arithmetic

English US QWERTY, printable ASCII 32–126. Correct printable attempts advance;
wrong attempts leave the target. Controls are not mistakes. Backspace retracts
only the current passage, resets streak, and never removes historical attempts.
Counters A, K, E, net progress N and active OS ticks are explicit 16-bit words;
A = K + E. Stop a run before any counter overflows. Subtraction of successive
16-bit tick samples handles wrap; updates occur every OS tick. OS clock is
1,193,182 / 65,536 Hz (SPEC.md §37), not an assumed 18 Hz integer clock.
Accuracy is nearest integer 100*K/A. WPM is floor(12*N/active_seconds), spaces
and punctuation included; no attempts/time displays a dash. Intermediate
products use checked two-word arithmetic. A product proven to fit one word
uses native word multiply/divide with exactly the same floor and saturation;
larger products retain the two-word path. Pauses exclude elapsed ticks.
Tick conversion may use the upper estimate `floor(ticks*3600/65536)` and
one exact PIT-threshold correction: the estimate exceeds the true quotient
by at most one over the entire word range. Byte products keep every
intermediate within one word; the final result remains the exact PIT ratio.
Finished statistics freeze exactly once. Challenges have a three-second ready
countdown, then 60/180/300 active seconds and expire without keyboard input.
The native adapter reports its initial scene visible exactly once before
starting that countdown: `bt_ready_visible(now)` resets only a READY session's
last tick, remaining ready ticks (55) and displayed count (3). Initial drawing
does not spend ready or scored time. ACTIVE/finished sessions ignore this call.
Focus loss, manual pause or display transition makes a challenge unranked.

24 authored lessons declare keys, introduced characters, instructions and
drills. A complete run at ≥95% masters it; any lesson stays selectable.
Practice uses its own reproducible PRNG and four valid groups: home row,
all letters, digits, and mixed letters/digits/punctuation. Native and compact
faces label these same categories; the digits group contains digits only. Weak keys require
20 samples, are ranked by error ratio, and report their samples. Records are
separate for each duration and difficulty. Achievements are persistent bits,
awarded from completed runs or measured streak events, never from repaint.

## 3. Drawing and input

Use apps/breadxt/draw.inc and apps/cc/gk.inc (SPEC.md §130.6, §133), not
private desktop video code. Compose opaque strips and commit through bx_out.
The measured exception is a medium/full key's thin state indicator on a known
flat face: existing native frame/hline and opaque font_run may update it
directly under the window's armed clip/graphics ownership. Reserved badge
columns and indicator bounds must not touch label or bevel pixels; full damage
paint composes the identical pixels. Compact keys retain opaque bands.
Cache and invalidate independent regions: title/navigation, portrait/caption,
prompt/input, statistics, keyboard cells, footer. A keystroke never repaints
the whole window. Unchanged timer passes skip cell/key scans before composing
any band. A timer services timing/audio before optional drawing. Before an idle redraw,
a non-consuming BIOS keyboard peek may defer that drawing so the OS can
dispatch a waiting key. It never reads a key or changes scoring; all scored
characters still come through the normal OS callback.
Input feedback precedes cosmetic portrait/caption work; cosmetic
updates coalesce to the newest expression. Caption changes paint at most four
physical scanlines per idle pass; portrait animation follows3.1’s precomputed
bounded strips. They are bounded so they cannot hold
the UI through a long animation. Full native damage paints remain complete. Layout derives from live content and display geometry.
Color roles are navy/cobalt, cyan rules, yellow title/target, white text,
green accepted, red mistake, and finger zones. Mono uses symbols/outlines.
All keys include required punctuation; shifted characters identify their
physical key and the opposite hand's Shift. Mouse keyboard clicks are hints,
never scored characters. Letter shortcuts exist only outside active typing.
Native and CGA faces share one pure US QWERTY map for physical base key,
finger name and opposite-hand Shift; the CGA row includes backquote/tilde.
Results with no printable attempts show accuracy as a dash and useful
start-typing guidance. The noncompact result face puts portrait/caption beside
larger statistics; the compact face retains all result actions.
CGA keeps a190-byte ASCII-to-key-cell table, built from that shared map at
scene entry. Static keyboard rows survive ordinary input; only old/new
target and mistake cells change. Its shadow compare skips identical rows.

Window fullscreen is SPEC.md §11.2's native latch. CGA four-color gameplay
uses SPEC.md §53's exclusive bracket and FSXM_CGA320; its compact renderer
is confined to that bracket, uses returned FSI geometry and VRAM segment,
and returns through FSX. Desktop drawing is prohibited in a foreign mode.
VESA uses the installed OS display driver and live dimensions, including
800×600 and 1024×768 when advertised; there is no private VBE driver.
F1 help, F2 lessons, F3 master mute, F4 pause, F6 fullscreen, Escape pause/menu.
Fullscreen transitions preserve session position, profile and seed.
Return to typing requires intentional resume; transition keys are not scored.
Native geometry changes also pause, including an OWNBG pure shrink that owes
no resize/damage callback. The package compares live geometry at UI service
boundaries and excludes the interval since its last serviced active tick.
Focus loss uses that same conservative observed boundary so a delayed UI
service cannot turn a covered/dragged challenge into a ranked deadline finish.
Help, Settings and Records opened from a live or paused lesson return to its
Pause screen. Returning from these views never silently starts a fresh run.

### Shared VGA presentation

The full-size native face uses the gold masthead in the supplied VGA sheet,
a cyan rule, a selected navigation tab, dimensional button faces, and a
separate shortcut strip. At 480 × 128 the established compact geometry and
keyboard controls remain available. Text, controls, counters, and keycaps
are live drawables. A monochrome masthead mask is stored in secondary art
record 7, using the shared font renderer's 16-pixel glyph strips. Body,
keycap and prompt type reuse the shipped Liberation Serif bold strikes. Five
24 × 24 transparent achievement badges use records 8–12 of the same bank.
The footer music-note meter follows the active soundtrack on the guest clock.
Four panel-sized scene cards show the lesson tutor, trophy, pointing reaction
and listening pose on their respective screens. Their offsets immediately
follow part 5's 16-byte header. Each opaque image stores width and height followed by four consecutive
bitplanes, most significant bit first. The package centers the card at a
byte boundary, intersects it with the active paint band, then copies only
those row spans into the compositor. Window damage and the final display
transfer still go through the shared drawing API. The mono and low-memory
paths use the static portrait. The four-color exclusive mode retains its
character-cell update path.

### 3.1 Original-grid portrait animation

The supplied six-column/five-row portrait sheet is authoritative: idle/blink,
talk, praise, roast and facepalm. The original PNG and exact crop/palette source
metadata are retained. Static band composition uses the selected original-grid
pose over a scenic crop from the supplied lesson artwork; a helper at explicit
coordinates is safe inside a full menu/body band. Caller owns frame/caption.

Clock service is independent of scoring and rendering (cosmetic selection
is an eager-overlay helper behind a small resident clock entry): `bt_face_clock(now)`
selects idle/blink, a short talk loop on a neutral caption, or one-shot praise,
roast/facepalm from the actual reaction expression/caption change. Reaction frames have
five-OS-tick dwell (about3.6frames/s), talk three ticks and blink two ticks; reaction sequences play once then settle
to idle. A new reaction may supersede an old one, but a selected frame stays latched
until all its changed strips have actually committed. The clock advances a
one-shot only after completion and its minimum dwell; delayed painting does
not skip its remaining poses. Neutral idle is a completed stable frame between
blinks. Cosmetic jobs receive alternate idle opportunities before deferred mirror,
footer, counter and hint work; urgent typing feedback keeps priority.
Roomy PLAY paints a white speech bubble with a black outline and small tail.
Its black cached caption text uses a byte-aligned interior. Incremental caption
jobs clear only that white interior and compose text there; border, tail and
portrait remain static until full damage. Compact captions keep adapter-readable
ink/paper and the existing bounded work policy.
Caption line breaks are cached per caption/font/width using the existing glyph
advance table. Only intersecting fitted lines compose glyphs; four-row updates
do not repeat whole-message wrapping or text-fit work.
Caption strips and portrait strips share those opportunities fairly; blink
gets its small eye update first. There is no unbounded frame queue.

The five optional movable parts 3–7 separate shape/strip signatures (3),
menu and badge art (5), and native 96 × 120 four-plane poses (4, 6, 7).
Each raw bank holds ten complete VGA poses. Source rows are 12 bytes and
planes are 1,440 bytes apart. Native color uses the normal clipped planar
transfer for the changed vertical extent, directly from the resident asset
bank. If the display refuses planar transfer, compose a 24-row strip through
the usual color fallback. Small and monochrome poses use the directory's
bounded compressed strips. Initialization fetches only the directory/small
motion part3 and, on color displays, scene part5. Private UI entry
`ovl_bt_face_prepare()` fetches the three raw banks only at explicit Start
or Resume, before sampling the scored clock. They remain held through
PLAY/PAUSE, including focus loss; `bt_face_release()` drops them at the next
cold-screen service boundary (HOME, RESULTS, HELP, SETTINGS or RECORDS).
Returning from a cold screen to PAUSE does not reload them; explicit Resume
re-arms the large tutor. No timer, idle painter or damage callback fetches
a raw bank. Small motion and scene sources stay available on those screens.
Every painter resolves the current segment before reading a movable bank.
Mono loads only part 3. A color guest with less memory can keep the small
animated tutor and scene cards independently of the large raw pose banks.
Static portraits cover missing motion data and read-only secondary instances.

### 3.2. Cold menu compositions

Roomy native content (at least600x360) uses the supplied lesson, settings,
records and result compositions: a selectable lesson table with real best
accuracy/status and the selected drill/key preview; grouped sound/display
controls with visible current states; duration-specific record tables,
measured mastery/streak/achievement gauges and seven achievement cards;
and result statistics beside a portrait/caption and actual weak-key caps.
Home, profile, help, pause and test setup reuse the same framed-card language.
All drawn controls retain the existing BT_A actions. Selection, paging and
preview derive from existing UI/profile/session fields; painting never changes
them. Display/output labels describe existing OS choices and do not fabricate
volume, device or voice controls. Compact geometry retains its dense layout
and every existing action. Menu composition helpers are cold overlay code,
use the existing opaque band renderer, allocate no claims and add no timer
work to PLAY. New literals must fit the current resident cap; fonts, common
buttons/header/footer and portrait scene assets remain shared helpers.
Roomy Settings renders Music/Effects/Key ticks as labels, small boolean
switches and segmented enabled-state indicators (never adjustable volume).
Roast choices are direct actions104..106 for Gentle/Classic/Extra; the existing
R shortcut still cycles. Actions107/108 request Windowed/Full screen and call
the existing toggle only when the current latch differs. Current adapter and
screen dimensions come from OSAPI_WM_DISPLAY. Compact Settings retains its
existing controls. Artwork composition remains unchanged by these controls.


Part 5's image directory at bytes16..43 contains four scene
offsets followed by five color and five grayscale16x16 badge offsets. IDs
266..270/271..275 select these badges while bank5 is armed; every draw restores
bank1 before fonts/sound use. Badges derive from committed24px source masks,
retain transparent palette index13, and remain visible in short native plaques.

### 3.3 Native PLAY proportions and bounded updates

The native lesson/practice/challenge face follows reference designs6,8,9:
one quarter-width portrait panel at the upper left, a framed typing panel
and separate framed statistics at the upper right, and a deep five-row
physical keyboard across the lower body. The shared header/footer retain
their existing live-content bounds. At624×420 content the keyboard row
pitch is31pixels and the left panel is152×174pixels: a96×120scene fits above
three caption lines. Shorter displays use the existing40×48portrait fallback;
content below280pixels high retains the complete compact keyboard/prompt
layout down to480×128. No required punctuation, modifier or physical key
is removed to copy the simplified keyboard in a reference image.

The typing panel has its own lesson/challenge title, two prominent target
rows, an inset accepted-input row, next-key/finger/Shift or ready-count hint,
and a progress meter. A challenge meter represents remaining active time;
its panel header also shows a prominent minutes:seconds countdown.
other runs represent accepted passage progress. Speed, accuracy, streak,
active/remaining time and error totals keep separate cached statistic cells.
The white spacebar spans 42 of the 60 keyboard layout units with four
modifier keys at the edges. Printable keys use the packaged 16-pixel
legends when their caps have room. Beveled/chamfered keycaps, strong black legends and finger-zone colors are
composed on full damage; the target frame, mistake/correction badge and
held-modifier line update only their reserved flat indicator pixels. The
five-pixel correction/mistake badge uses clipped native fill/hline spans in
its5×8reserved patch, so even a16pixel-wide bold legend cannot overlap it.
Large key labels remain centered within their label region, clear of badges.
The accepted-input caret changes phase every nine guest UI ticks. READY uses
a prominent countdown hint and GO remains visible for nine UI ticks after
READY becomes ACTIVE. These visual clocks are independent of scored ticks.
The accepted-input label reserves its measured font width. Caret changes
wait for queued accepted text to finish, and a new dirty range starts with
only its changed cells. Periodic statistics repaint their value bands;
their frames and static labels belong to full damage.
Each idle pass advances at most one accepted-input cell or four scanlines
of one hint/statistic value, so queued typing can run between those chunks.
After cache/layout preparation, native PLAY rechecks BIOS keyboard and OS
event queues before beginning deferred work; urgent feedback and full damage
remain complete.

Urgent target cells and physical-key feedback precede statistics, hints,
progress and portrait cosmetics. Progress damages only the changed fill
span. An unchanged timer pass does not rebuild the keyboard or prompt.
The100ms feedback target remains a native-guest measurement gate; host
geometry/dirty-area checks do not claim XT timing. Painting changes no
session, scoring, PRNG, persisted profile or audio state.

## 4. Resources and budgets

BORATYPE is eight characters, a legal 8.3 stem. All runtime assets are package
parts. Fonts reuse the committed WIN16 font
strikes. Artwork masters, prompts and exact-size palette sprite source are
committed; deterministic conversion is offline. Source PNG is never parsed
by the guest. Resident code+data+BSS must fit §73's 63.5KB ceiling; split cold
menu/result/file routines into an overlay when needed, never raise a budget.
Geometry arrangement and non-typing menu-key handling are cold overlay work;
the unchanged-layout check and active lesson-key route remain resident.
Initial allowances: engine and lesson data 18KB, UI and platform 26KB,
sound/reaction code and data 8KB, BSS 10KB. Art part ≤64KB, audio part ≤16KB,
drawing band 8KB (512 paragraphs). Music and effects require no application
audio heap claims. Every claim
is movable with a relocation callback, or explicitly pinned during transfer.
The final evidence records actual costs; these are planning allowances.

## 5. Audio and reactions

Original menu, lesson and challenge loops (30–90 seconds), plus three
stingers and nine or more effects, compiled with tools/os88gkmus.py.
Reuse gkmus.inc's speaker/three-channel FM routing and priority leases.
Independent music/effects toggles; key ticks default off. Pause/focus
loss silences music, exit releases every app-owned resource. No audio file
read, decode, busy wait or blocking clip call in paint or input.
Mute, audio-setting changes and pause apply their transport policy before
drawing the resulting screen. Informational views over a paused lesson keep
its music held until deliberate resume or return to the main menu.

At least 48 tagged original captions, at least 24 roasts; Gentle/Classic/
Extra, Classic default. Three errors, <85% after 20 attempts, correction
bursts, poor results, recovery and records are actual triggers. Last four
captions cannot repeat immediately; the default reaction gap is12seconds. Jokes
never change scoring. Slow jokes require baseline and elapsed-time evidence.
Caption and portrait animation clocks are independent from scoring.
A no-input completion receives neutral guidance, never success praise; it
does not create a record or an achievement.

The user-selected scope is original music, sound effects and visual messages.
There are no sound clips, voice packs, speech controls, PCM transport or audio
importer. Reactions never depend on sound being enabled: muted and readonly
instances retain their captions, expressions, cooldowns and focused idle jokes.
The BTT1 voice byte remains an unused legacy field for format compatibility;
it cannot enable an audio feature.

## 6. Persistence

Canonical little-endian fields, magic/version/length/checksum, no struct dumps
or pointers. Four names ≤12 ASCII characters, 24 lesson results per profile,
bounded per-key attempts/errors, achievements and 3×difficulty challenge
records. Validate into a staging store before replacing live data. Corrupt,
truncated or newer-version files preserve the live session. Save only at
result/settings/profile boundaries; errors stay visible and play continues.
Use alternating generations or validated temporary/backup replacement as
supported by the filesystem; do not delete the last valid save first.

Canonical BTT1 v1 image: 2,136 bytes. Magic at0, version LE16 at4,
length LE16 at6, generation LE16 at8, selected/music/effects/voice/keyclick/
roast/display bytes10–16, reserved zero at17, Fletcher16 at18 (those two
bytes treated as zero), and four explicit529-byte profiles from20.
Alternating BORATA.BTT/BORATB.BTT generations are compared modulo65536;
a header generation is only a hint before full validation. A read-back must
match written generation and checksum before it may replace in-memory state.


## 7. Verification boundary

Host harness exercises production engine, target-width overflow, independent
score examples, lesson declarations, pause/wrap/deadline, deterministic
practice, achievements, reactions, codec corruption and music/effect banks.
Native validation must drive installed packages with os88ui, inspect captured
screens, audition captured audio and measure XT cycles; host tests cannot
substitute. The final pass runs affected soak rows together, then test-full
alone and budget-check. Evidence marks pass/fail/blocked per real scenario.
