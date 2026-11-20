# Borat Teaches Typing

Binding package contract for BORATYPE.O88, SPEC.md §134. This describes the
intended implementation; the checked plan and evidence distinguish what has
been built and observed. The supplied build prompt and nine images define
the product scope. No external publication is authorized by this task.

## 1. Platform and ownership

Native C, one translation unit, SmallerC → cc8086 → NASM → CC_PACKAGE.
No kernel resident bytes or new API slots. The C restrictions in SPEC.md §73
apply. A package instance owns one window and its session. Four local profiles
share a bounded versioned store on the package volume's SYSTEM/APPDATA.
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
products use checked two-word arithmetic. Pauses exclude elapsed ticks.
Finished statistics freeze exactly once. Challenges have a three-second ready
countdown, then 60/180/300 active seconds and expire without keyboard input.
The native adapter reports its initial scene visible exactly once before
starting that countdown: `bt_ready_visible(now)` resets only a READY session's
last tick, remaining ready ticks (55) and displayed count (3). Initial drawing
does not spend ready or scored time. ACTIVE/finished sessions ignore this call.
Focus loss, manual pause or display transition makes a challenge unranked.

24 authored lessons declare keys, introduced characters, instructions and
drills. A complete run at ≥95% masters it; any lesson stays selectable.
Practice uses its own reproducible PRNG and valid groups. Weak keys require
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
updates coalesce to the newest expression and are bounded so they cannot hold
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
