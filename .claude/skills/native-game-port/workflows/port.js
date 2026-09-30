// native-game-port / workflows/port.js (see ../SKILL.md)
// args: object or JSON string { repo, worktree, ref, name, stem, reports, scope, skipScout }; defaults below.
export const meta = {
  name: 'native-game-port',
  description: 'Scout, plan, implement in waves, review and verify a native 8086 game remake for os8088',
  phases: [
    { title: 'Scout', detail: 'five parallel deep reads: game logic, graphics, audio, XT perf techniques, integration conventions' },
    { title: 'Plan', detail: 'architect writes docs/plans/${NAME.toUpperCase()}-PLAN.md and the wave list' },
    { title: 'Waves', detail: 'implement -> 3 review lenses -> fix -> verify on emulator, per wave' },
    { title: 'Close', detail: 'completeness critic, SPEC/docs/index/test registration' },
  ],
}

const A = typeof args === 'string' ? JSON.parse(args) : (args || {})
const WT = A.worktree || '/tmp/game'
const REF = A.ref || '/path/to/reference'
const REP = A.reports || WT + '-reports'
const NAME = A.name || 'Game'
const LOW = (A.stem || NAME).toLowerCase()
const SCOPE = A.scope || 'a playable single-player core; say what is cut'
const MODE = A.artMode || 'recreate' // recreate | extract | original
const COMMONPOLICY = "NO DEPENDENCY ON THE REFERENCE: a plain 'make' on a machine without the reference directory must build the whole game, and no test may require it - a comparison against the reference must SKIP cleanly when it is absent. No ROM image, CHR dump or sample/note stream is read at build or run time. Whatever is taken from the reference is taken ONCE, at authoring time, by a one-off tool that make never runs, and only the result is committed, in OUR format, with a provenance line in apps/" + LOW + "/art/README.md. A fast-tier provenance gate (tests/unit/t_" + LOW + "_clean.py) fails the build if a reference path or importer creeps back into make."
const LEVELS = "LEVELS AND TABLES MATCH THE CARTRIDGE: course/level layouts, piece/obstacle grammar, speed and timing tables, par times and rules are TRANSCRIBED from the reference once (one-off tool, e.g. tools/" + LOW + "_transcribe_once.py) into committed text sources under apps/" + LOW + "/ in our own format, so the game plays the original's levels. Deliberate deviations are listed with reasons in tests/" + LOW + "_ref_deviations.txt."
const ARTS = {
  recreate: "GRAPHICS AND AUDIO MATCH THE CARTRIDGE BY RECREATION: sprites, tiles, backgrounds, HUD layout, screens and sound roles must LOOK and SOUND like the original (same poses/frames, sprite roles, palette feel, composition, layout measurements, scale and animation timing) but every committed pixel and note is newly made. Study the reference at authoring time (render tile sheets, read layout numbers) and write what you see into generation prompts and procedural generators. Do NOT attach reference frames/tiles as image inputs and do not commit or trace the reference's bytes. Make masters with the image generator (see SKILL.md 'Art generation': codex exec first, then the session's built-in imagegen), commit the masters plus a PROMPT.md with the exact prompts, and derive production assets with a deterministic host tool that --selfcheck asserts.",
  extract: "GRAPHICS AND AUDIO ARE EXTRACTED EXACTLY, ONCE: a one-off tool (never run by make) converts the reference's tiles, palettes and sound data into committed assets in our own format, so the game matches the cartridge pixel for pixel. The user chose this knowingly (licensing is theirs to weigh); say so in the PR. The committed output is then the only source of truth and the build never reads the reference.",
  original: "ALL ART AND SOUND ARE ORIGINAL DESIGNS in the spirit of the game, not copies: no reference pixels or notes are read or traced. Made with the image generator and procedural host tools, committed with prompts."
}
const POLICY = "ART AND ASSET POLICY (user decision, binding, overrides anything in scout reports or an earlier plan draft). " + COMMONPOLICY + " " + ARTS[MODE] + (A.levels === 'original' ? " Levels are also original." : " " + LEVELS) + " The scout reports' import recommendations are void; use their sizes and budgets only."

const COMMON = `
CONTEXT
- Task: a native 8086 remake of ${NAME} to os8088, in the style of DrMarco (SPEC.md section 100, apps/drmario/) and 1942 (SPEC.md section 101, apps/1942/). It is a native remake, NOT an NES emulator: the simulation is our own, guided by the disassembly. The primary goal is BLAZING SPEED on a 4.77MHz IBM PC/XT (8088), on VGA and CGA (and Hercules if the DrMarco precedent shows it is cheap).
- Work ONLY in the git worktree ${WT} (branch game/${LOW}). Never touch /Users/jggonz/Repos/os8088 (the main checkout). Do not commit, do not push, do not use git add -A/-u; do not run pkill with broad patterns (other worktrees run emulators) - kill only by pidfile.
- ${POLICY}
- Reference source for game LOGIC study only (read-only, external): ${REF} (disassembly/source files, RAM maps, CHR/asset dumps, any replay files). 
- Repo rules: CLAUDE.md (already in your context) is binding. Read docs/INDEX.md before inventing a mechanism. Text via font_run. 8086-only NASM. SPEC.md is updated BEFORE/with each change (new section, next free number after 101). Never spend kernel bytes: this is a package, no new API slots, no kernel change.
- Reports go to files under ${REP}/ (write the full report there; your final message is a SHORT summary plus the file path). Messages truncate; files do not.
- Emulator tests: MartyPC is the timing-faithful 4.77MHz XT model. A fresh worktree lacks build/martypc: copy it from /Users/jggonz/Repos/os8088/build/martypc (cp -R, not symlink) and run 'make marty' if needed. Use short paths for sockets. Run 'make' output through a check for 'Error' (exit-code hygiene: wrappers can hide 'make: *** Error').`

const SCOUTS = [
  { key: 'game', prompt: `Deep-read the game-logic sources under ${REF}. Produce a port-oriented account of the GAME LOGIC: main state machine, game modes (selection A/B, design track, Excitebike race, time trial), the rider physics (speed, gears/boost, temperature/overheat, lane change, jump pitch/landing, wheelie, crashes), the track data format and track-piece/obstacle/ramp/hurdle/mud/arrow definitions, the AI opponents, lap timer/rank/qualification, the horizontal scroll model, RNG, the demo/attract mode. For each: routine names, line ranges, RAM variables, constants and the rules in plain words, plus what to simplify for an 8088 and what must stay faithful for the feel. Also inspect the ${REF}/_misc replay .fm2 files: could they serve as deterministic regression input for a host-side reference model (what would that cost)? Output the report to ${REP}/scout-game.md.` },
  { key: 'gfx', prompt: `Deep-read the graphics side of ${REF}: the pattern/tile layout (pattern tables, which tiles are the bike/rider frames, track tiles, HUD font, backgrounds), the nametable/attribute logic, palettes per screen, sprite (OAM) usage per frame, the horizontal scrolling/nametable-wrap behaviour, and the HUD. Design the CONVERSION for os8088: the art pipeline the ART AND ASSET POLICY requires (a deterministic host tool like tools/1942assets.py reading only committed sources). Quantify sizes for VGA 16-colour planar/mode X, CGA 320x200x4 and Hercules 720x348 mono: tile cache size, track strip width, rider sprite frames, and what fits in the 64KB segments the OS gives packages (see docs/KERNEL-MEMORY.md, SPEC.md 101.1 for how 1942 packs pages). Recommend ONE resolution/scale per adapter (NES is 256x240) with the arithmetic. Output to ${REP}/scout-gfx.md.` },
  { key: 'audio', prompt: `Deep-read the SOUND side of ${REF} (APU driver, music engine, note tables, SFX for engine/jump/crash/wheelie/lap/pause, song data) and how apps/1942/audio.inc, pcm.inc, sfx/ and apps/drmario/audio.inc do audio on os8088 (PC speaker, AdLib, Sound Blaster, OSAPI sound slots in docs/INDEX.md). Recommend the cheapest audio design that keeps the XT frame budget: which music tracks to import as note streams by a host tool, the engine-pitch-follows-speed effect on the PC speaker, and per-frame CPU cost. Output to ${REP}/scout-audio.md.` },
  { key: 'perf', prompt: `Study, in ${WT}, the SPEED techniques of apps/1942/ (scroll.inc, video.inc, motion.inc, game.inc, 1942.asm) and apps/drmario/ (video.inc, anim.inc, game.inc), SPEC.md sections 100 and 101, PERFORMANCE.md (Part 5, and every Set about these games / FSX / blit / CRTC scrolling), tests/n1942.py, and the FSX fullscreen API in docs/INDEX.md and SPEC.md (FSXM_MODEX, FSXM_CGA320, FSXW_VSYNC, latch copies, write modes). Decide the scroll direction of ${NAME} (1942 scrolled vertically, Excitebike horizontally) and work out, with cycle arithmetic on 8088 timings (fetch floor 4.34 clocks/byte, see PERFORMANCE.md Part 2), the fastest scheme per adapter: CRTC start-address panning in mode X / CGA (byte granularity vs 4px vs 8px, sub-pixel via panning register 3C0h/ 0x13 on VGA, CGA has none), how new track columns are written into the ring, compiled sprites, dirty rectangles, page flipping, and vsync waiting. Produce a per-frame budget table for a 4.77MHz 8088 at a target frame rate (state the target and why; NES is 60fps, the XT will not reach it, choose a truthful target like 15-30 with fixed-timestep simulation) and name every trap (64KB latch limits, CGA bank split at 8192, snow on real CGA, SS != DS). Output to ${REP}/scout-perf.md.` },
  { key: 'integ', prompt: `Map every file and convention an os8088 game package touches by reading git history in ${WT}: 'git show --stat' for commits bde7bbe8 (1942, #202) and 13e20ed8 (DrMarco, #204) and their diffs of Makefile, tests/suite.py, SPEC.md, docs/INDEX.md (tools/os88index.py), PERFORMANCE.md, .gitignore, tests/unit/t_livefull.py EXEMPT_DIRS / live payload, tools/checkdocs.py, vm/ 86Box configs, the 'local' disk class row in SPEC.md ~line 40907, README/site claims. Produce an exact CHECKLIST for a new package 'apps/${LOW}/' (the .O88 (8.3-safe stem), make targets ${LOW} / ${LOW}disk / ${LOW}test, geometry list, how the loading screen and splash work, how tests are registered, what fails 'make' if omitted). Then actually run 'make' (or the narrowest target) in ${WT} once to learn the baseline: does it pass, how long, any pre-existing failure. Output to ${REP}/scout-integ.md.` },
]

const PLAN_SCHEMA = {
  type: 'object',
  properties: {
    planPath: { type: 'string' },
    waves: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          id: { type: 'string' },
          title: { type: 'string' },
          scope: { type: 'string' },
          acceptance: { type: 'string' },
        },
        required: ['id', 'title', 'scope', 'acceptance'],
      },
    },
  },
  required: ['planPath', 'waves'],
}
const FIND_SCHEMA = {
  type: 'object',
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          file: { type: 'string' }, line: { type: 'number' },
          severity: { type: 'string' }, problem: { type: 'string' }, fix: { type: 'string' },
        },
        required: ['file', 'problem', 'fix'],
      },
    },
  },
  required: ['findings'],
}
const VERIFY_SCHEMA = {
  type: 'object',
  properties: {
    pass: { type: 'boolean' },
    evidence: { type: 'string' },
    failures: { type: 'array', items: { type: 'string' } },
  },
  required: ['pass', 'evidence', 'failures'],
}

// ---- Scout
phase('Scout')
const scouted = await parallel((A.skipScout ? [] : SCOUTS).map(s => () =>
  agent(`${COMMON}\n\nYOUR SCOUTING JOB (${s.key}):\n${s.prompt}`, { label: `scout:${s.key}`, phase: 'Scout' })))
log(`scouts done: ${scouted.filter(Boolean).length}/${SCOUTS.length}`)

// ---- Plan
phase('Plan')
const plan = await agent(`${COMMON}

You are the ARCHITECT. Read all five scout reports in ${REP}/ (already written; the ART AND ASSET POLICY above supersedes their import advice). If ${WT}/docs/plans/${NAME.toUpperCase()}-PLAN.md already exists, REVISE it instead of starting over, but rewrite every graphics/audio/level/asset decision to follow that policy, stating how the original art is authored and generated reproducibly (tool and inputs) and the hard byte budget it must fit. Reports in ${REP}/ (scout-game.md, scout-gfx.md, scout-audio.md, scout-perf.md, scout-integ.md), fill any gap by reading the sources yourself, then write ${WT}/docs/plans/${NAME.toUpperCase()}-PLAN.md: the design record. It must decide and justify with arithmetic: per-adapter video scheme (VGA / CGA / Hercules-or-not), scroll scheme, memory map of the package's claims, sprite representation, simulation timestep and target frame rate on a 4.77MHz XT with the per-frame cycle budget table, game scope (user scope: ${SCOPE}; say what ships, what is cut, and why), the input scheme (keyboard, and how it maps to A/B/Up/Down/Left/Right), audio design, art, sound and level authoring pipeline per the ASSET POLICY (incl. image-generation prompts and where masters are committed), the file/section layout of apps/excitebike/, and how it will be tested (a MartyPC frame-rate gate priced in emulated cycles, screenshot gates on each adapter, a host reference model the guest must equal step for step, a pixel-exact reference renderer, each with a negative control, and any check against the disassembly skipping cleanly when it is absent).
Then split the work into 4-7 sequential WAVES, each independently buildable and verifiable on the emulator, each with acceptance criteria that are a command or a measurable observation. Suggested order (adapt freely): 1 art/sound/level compilers (and generated masters) + package skeleton + build/disk/SPEC/tests registration + splash; 2 video + horizontal scroll engine with a scrolling test track at the target rate; 3 rider physics, controls, obstacles/ramps/crashes, HUD; 4 lap timer/race flow/AI/modes; 5 audio; 6 polish, all adapters, all disk geometries, docs. Return the plan path and the waves.`, { label: 'architect', phase: 'Plan', schema: PLAN_SCHEMA })

if (!plan || !plan.waves || !plan.waves.length) { log('no plan produced'); return { error: 'plan failed' } }
log(`plan: ${plan.waves.length} waves -> ${plan.planPath}`)

// ---- Waves (sequential: each builds on the last, one writer at a time)
phase('Waves')
const results = []
for (const w of plan.waves) {
  log(`WAVE ${w.id}: ${w.title}`)
  const impl = await agent(`${COMMON}

You are the IMPLEMENTER of wave ${w.id}: ${w.title}.
Read ${plan.planPath} fully, and the scout reports in ${REP}/. Previous waves' results: ${JSON.stringify(results.map(r => ({ id: r.id, verify: r.verify && r.verify.pass, notes: r.summary })))}.
SCOPE: ${w.scope}
ACCEPTANCE: ${w.acceptance}
Rules: write real, complete code - no stubs that pretend. Speed is the product: every hot path priced in 8088 cycles in a comment where non-obvious; no per-pixel conversion in the frame loop; compiled/latched/REP-string operations; nothing writes a pixel twice. Update SPEC.md in the same wave. Register anything new in tests/suite.py (with a budget note) and run 'make' plus your wave's tests until green, checking for 'Error' in output. If an acceptance criterion cannot be met, say so precisely rather than weakening it silently. Write your wave log (what changed, measured numbers, open issues) to ${REP}/wave-${w.id}-impl.md.`,
    { label: `impl:${w.id}`, phase: 'Waves' })

  const lenses = [
    ['8086/memory-safety', 'Only-8086 instructions (cpu 8086 -w+error), SS != DS addressing, ES/DS restoration on OSAPI boundaries, 64KB segment and 512-byte-alignment rules, RETF compiled sprites, buffer overruns on the ring/claims, register preservation, section discipline, CGA bank-boundary splits.'],
    ['xt-performance', 'Cycle cost on a 4.77MHz 8088 (PERFORMANCE.md table): per-frame budget vs the plan, any per-pixel loop, any double-write, any full repaint, any visible-redraw/flash risk, vsync/CRTC misuse, wasted fetch bytes. Measure or count; do not guess. Say which findings are measured.'],
    ['fidelity-and-correctness', `Faithfulness to the disassembly at ${REF} (physics constants, track pieces, timing, scoring) versus what the plan chose to simplify; logic bugs, off-by-ones in scroll wrap, state-machine holes; test quality (would the tests fail if the feature broke?); SPEC.md/doc accuracy versus the code.`],
  ]
  const reviews = await parallel(lenses.map(([k, focus]) => () =>
    agent(`${COMMON}

REVIEW wave ${w.id} (${w.title}) in ${WT} through the lens "${k}": ${focus}
Inspect the uncommitted changes (git -C ${WT} status / diff) and the wave log ${REP}/wave-${w.id}-impl.md. Report only REAL, specific defects with file:line and a concrete fix; verify each by reading the code (or running it) before reporting. Also write the list to ${REP}/wave-${w.id}-review-${k.replace(/[^a-z]/g, '')}.md. Do not edit source files.`,
      { label: `review:${w.id}:${k}`, phase: 'Waves', schema: FIND_SCHEMA })))

  const all = reviews.filter(Boolean).flatMap(r => r.findings || [])
  log(`wave ${w.id}: ${all.length} review findings`)
  let fixNote = 'no findings'
  if (all.length) {
    fixNote = await agent(`${COMMON}

You are the FIXER for wave ${w.id}. Apply fixes for these review findings in ${WT}, but FIRST re-check each against the code and skip (with a reason) any that are wrong or already handled. Do not regress performance to fix style. Then re-run 'make' and the wave's tests until green.
FINDINGS:
${JSON.stringify(all, null, 1)}
Write what you fixed/skipped to ${REP}/wave-${w.id}-fix.md and reply with a 3-line summary.`, { label: `fix:${w.id}`, phase: 'Waves' })
  }

  const verify = await agent(`${COMMON}

You are the INDEPENDENT VERIFIER of wave ${w.id} (${w.title}). You did not write this code; trust nothing in the logs. In ${WT}: run 'make' (must be clean, no 'Error'), the wave's registered tests, 'make test-fast', and the ACCEPTANCE criterion: ${w.acceptance}
Drive the actual build on the emulator (MartyPC for timing and CPU-priced numbers; QEMU/QMP screenshots via tools/shot.py for the picture) and LOOK at screenshots (Read the PNG) on every adapter the wave touches - crop and zoom before concluding anything. Record numbers (emulated cycles per frame, fps). pass=true only if every acceptance item is demonstrated with evidence; otherwise list each failure precisely. Save evidence images under ${REP}/wave-${w.id}-*.png.`,
    { label: `verify:${w.id}`, phase: 'Waves', schema: VERIFY_SCHEMA })

  let v = verify
  if (v && !v.pass) {
    log(`wave ${w.id} failed verify (${v.failures.length}); one repair round`)
    await agent(`${COMMON}

Wave ${w.id} FAILED independent verification. Repair the code in ${WT} so it passes, without weakening the acceptance criterion (${w.acceptance}). Failures:
${JSON.stringify(v.failures, null, 1)}
Evidence: ${v.evidence}
Re-run make and the tests until green; log to ${REP}/wave-${w.id}-repair.md.`, { label: `repair:${w.id}`, phase: 'Waves' })
    v = await agent(`${COMMON}

Re-verify wave ${w.id} (${w.title}) from scratch, independently, as before: make clean of errors, registered tests, make test-fast, and ACCEPTANCE: ${w.acceptance}. Look at real screenshots. Be strict.`,
      { label: `reverify:${w.id}`, phase: 'Waves', schema: VERIFY_SCHEMA })
  }
  results.push({ id: w.id, title: w.title, verify: v, summary: (typeof fixNote === 'string' ? fixNote : '').slice(0, 400) })
  log(`wave ${w.id}: verify ${v && v.pass ? 'PASS' : 'FAIL'}`)
}

// ---- Close
phase('Close')
const closing = await parallel([
  () => agent(`${COMMON}

COMPLETENESS CRITIC. Compare the finished work in ${WT} against ${plan.planPath} and ${REP}/scout-integ.md's checklist. List what is missing or unverified (modes promised but absent, adapters untested, disk geometries not built, docs/SPEC/INDEX/PERFORMANCE entries, live-payload/EXEMPT registration, site/README hooks, suite registration, gitignore for imported assets). FIX every gap that is mechanical; report the rest. Run 'make' and 'make test-full' at the end (check for 'Error'; run alone, no concurrent make). Write ${REP}/close-critic.md.`, { label: 'critic', phase: 'Close' }),
  () => agent(`${COMMON}

FINAL PERFORMANCE AUDIT on MartyPC (4.77MHz 8088 model): measure the shipped build's steady-state frame time in emulated cycles on VGA and CGA during scrolling with the rider at speed, jumping and with obstacles, and record fps, plus boot-to-play load time. Add the measurements to PERFORMANCE.md as a new Set in the same style as the 1942/DrMarco entries. If a hot path is clearly improvable by a cheap change, do it and re-measure. Write ${REP}/close-perf.md.`, { label: 'perf-audit', phase: 'Close' }),
])

return {
  plan: plan.planPath,
  waves: results.map(r => ({ id: r.id, title: r.title, pass: r.verify && r.verify.pass, evidence: r.verify && r.verify.evidence, failures: r.verify && r.verify.failures })),
  closing: closing.map(c => (typeof c === 'string' ? c.slice(0, 600) : null)),
  reports: REP,
  worktree: WT,
}
