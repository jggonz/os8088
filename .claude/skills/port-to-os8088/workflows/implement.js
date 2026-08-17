// =============================================================================
// port-to-os8088 / workflows/implement.js  --  ONE WAVE OF THE IMPLEMENTATION
//
// Invoked by the port-to-os8088 skill (SKILL.md, step 7) ONCE PER WAVE:
//
//   Workflow({ scriptPath: "<repo>/.claude/skills/port-to-os8088/workflows/implement.js",
//              args: { repo: "<abs path of the os8088 checkout>",
//                      plan: "<abs path of docs/<NAME>-PORT-PLAN.md>",
//                      wave: <n>,
//                      name: "<package name>", dir: "<apps/<dir>>",
//                      sources: ["<abs path of reference repo>", ...],
//                      decisions: "<the user's answers so far, verbatim>",
//                      rounds: 2 } })
//
// One wave per run, on purpose: a blocker inside a wave comes back to the
// skill as a QUESTION and the skill asks the user before the next wave; and a
// run stays under 15 agents. Only ONE agent edits the tree at a time - the
// implementer, then the fixer - and every reviewer is read-only, because a C
// package is one translation unit and two hands in it do not merge.
//
// Returns { status: 'done' | 'blocked' | 'failed', report, questions[],
//           size, shots[], reviews }.
// =============================================================================
export const meta = {
  name: 'port-implement-wave',
  description: 'Implement one wave of a port plan, review it through three lenses, fix, and verify on QEMU',
  phases: [
    { title: 'Implement', detail: 'one agent writes the wave, builds through the gate, runs the harness, boots it' },
    { title: 'Review', detail: 'three read-only lenses: the four rules + budget, the redraw budget, fidelity to the source' },
    { title: 'Fix', detail: 'the findings are applied and the build re-run; up to `rounds` times' },
    { title: 'Verify', detail: 'a fresh agent boots the result and takes the screendumps the plan asked for' },
  ],
}

const REPO = args.repo
const SKILL = REPO + '/.claude/skills/port-to-os8088'
const ROUNDS = args.rounds == null ? 2 : args.rounds
const WAVE = args.wave

const READ_FIRST = `Read, in this order, before touching anything:
  ${SKILL}/LESSONS.md            (what the CWORD port learned - binding)
  ${args.plan}                   (the port plan - wave ${WAVE} is yours)
  ${REPO}/docs/C-TOOLCHAIN.md    (the four C rules, refusals and their fixes, the overlay)
  ${REPO}/apps/cc/os88.h         (the API; its header comment is the short version of the rules)
  ${REPO}/CLAUDE.md              (hard rules; the performance table; the two testing traps)
Precedent to copy, not to admire: ${REPO}/apps/cword/ (the C port of Word - its files, shim, hosttest/, Makefile rules), ${REPO}/tests/covl/covl.c (the overlay gate), ${REPO}/apps/cc/ccsmoke.c (the template).
Reference source(s) the UI is taken from, verbatim: ${(args.sources || []).join(', ')}
Decisions the user has already made (binding): ${args.decisions || '(none yet)'}`

const IMPL_SCHEMA = {
  type: 'object', required: ['status', 'report', 'questions', 'size', 'shots', 'files_touched'],
  properties: {
    status: { type: 'string', enum: ['done', 'blocked', 'failed'] },
    report: { type: 'string', description: 'what was built, how it was verified, the exact commands, and what a reviewer should look at' },
    questions: { type: 'array', items: { type: 'object', required: ['q', 'options', 'recommend', 'why'], properties: {
      q: { type: 'string' }, options: { type: 'array', items: { type: 'string' } }, recommend: { type: 'string' }, why: { type: 'string' } } },
      description: 'ONLY decisions the user must make. Empty when status is done and nothing is open' },
    size: { type: 'string', description: 'the os88pkg line verbatim (image=, bss=) and, if there is an overlay, the .OVL size; plus the frame-size max cc8086 printed' },
    shots: { type: 'array', items: { type: 'string' }, description: 'paths of screendumps taken, under build/port-shots/' },
    files_touched: { type: 'array', items: { type: 'string' } },
  },
}

const REVIEW_SCHEMA = {
  type: 'object', required: ['lens', 'verdict', 'findings'],
  properties: { lens: { type: 'string' }, verdict: { type: 'string', enum: ['pass', 'fix', 'fail'] },
    findings: { type: 'array', items: { type: 'object', required: ['severity', 'file', 'finding', 'fix'], properties: {
      severity: { type: 'string', enum: ['blocker', 'major', 'minor'] }, file: { type: 'string' }, finding: { type: 'string' }, fix: { type: 'string' } } } } },
}

const VERIFY_SCHEMA = {
  type: 'object', required: ['pass', 'report', 'shots'],
  properties: { pass: { type: 'boolean' }, report: { type: 'string' }, shots: { type: 'array', items: { type: 'string' } } },
}

const RULES = `Non-negotiable while you work:
- Build ONLY through the tree's rules: the plan's make target (\`make ${args.name.toLowerCase()}\` unless the plan names another - a CC_PACKAGE rule), the disk target, and the package's host harness. Never pass --no-gate or --quiet to cc8086.py. Read every refusal: the message names the fix, and the C function is the nearest _label: above the reported line in build/*.raw.asm.
- After every successful build, record the os88pkg size line. If resident image+bss passes 55,000 bytes, STOP adding and move per-command code to ovl_* (LESSONS.md, "the overlay") before continuing.
- Boot what you built: \`make test TESTAPPS=build/<img>\` (kill any stale qemu first: pkill -f qemu-system-i386; a stale one answers on build/qmp.sock with the OLD image), drive it with tools/mouse.py / tools/qmp.py, screendump with tools/shot.py --crop --zoom into build/port-shots/wave${WAVE}-*.png, and LOOK at the PNG (Read it) before believing it. Quit qemu when done (tools/qmp.py build/qmp.sock quit).
- Take every user-visible string, mnemonic, shortcut and dialog field from the reference source files the plan names. Do not invent UI. What you cannot carry is present and greyed with its FACT (SPEC 47), never silently missing.
- Do not commit. Do not touch apps/word, apps/cword or any other package. Do not raise KERN_BUDGET, KERN_CODE_MAX or APP_MAX_SIZE.
- If a decision is genuinely the user's (a scope cut, a name, spending an API slot, two references disagreeing), finish everything that does not depend on it, then return status "blocked" with the question and your recommendation. Do not guess and do not stop early for anything you can decide yourself.`

// ---------------------------------------------------------------- Implement
phase('Implement')
const impl = await agent(
`${READ_FIRST}

You are implementing WAVE ${WAVE} of the port plan for ${args.name}, in ${REPO}/${args.dir}. Wave 1 also creates the package from nothing: the .c and .asm shim (copy apps/cc/ccsmoke.asm's shape and apps/cword/cword.asm's), the CC_PACKAGE line and disk rules and phony target in the Makefile beside cword's (with an explicit prerequisite line for every #included .c/.h - make cannot see through #include), and the host harness skeleton under ${args.dir}/hosttest/ modelled on apps/cword/hosttest/ (a stub os88.h and a driver that includes the program). Later waves extend what is there.

${RULES}

Work until the wave's "done_when" is true on the glass and the harness passes, then return the report. Include in "report" the exact commands you ran, the size line, and where the screendumps are.`,
  { label: `implement:wave${WAVE}`, phase: 'Implement', schema: IMPL_SCHEMA })

if (!impl) return { status: 'failed', report: 'implementer returned nothing', questions: [], size: '', shots: [], reviews: [] }
log(`implement: ${impl.status}; ${impl.size}`)
if (impl.status === 'failed') return { ...impl, reviews: [] }

// ---------------------------------------------------------------- Review / Fix
const LENSES = [
  { key: 'rules', prompt: `THE RULES + BUDGET LENS. Read the diff (git diff; git status for new files) and the generated build/*.raw.asm if useful. Check: every addressable object is static; no struct by value; no long/float/double/bit-field; unsigned char where a byte is compared; frames small; no C between gfx_lock/unlock that is unbounded; the shim's CC_HAS_* match the C callbacks; the Makefile has a prerequisite for every #included file; nothing opens its own section; ES-relative window fields go through os88_win_*; ovl_* functions return a status and none has its address taken; no ovl_* is called from a worker task; the size line is honest and under 61,440 with a plan for the next wave's growth; the package/dir/image/vm names collide with nothing.` },
  { key: 'redraw', prompt: `THE REDRAW-BUDGET LENS (PERFORMANCE.md's rules as CLAUDE.md condenses them). Read the paint / key / click paths in the diff. Price them in gfx calls and glyph cells: does a keystroke repaint only what changed? Is there a shadow of the glass or an equivalent damage model? Is anything drawn twice (erase then draw)? Is a full repaint reachable from an ordinary keystroke, scroll or menu close? Does layout re-run more of the document than the edit could have changed? Do dialogs clamp to the live screen (vid_w/vid_h via os88) and stay within 12 rows? Would this be seconds on a 4.77 MHz 8088? Say what a keystroke costs, in calls and cells, from reading the code.` },
  { key: 'fidelity', prompt: `THE FIDELITY LENS. Open the reference source files the plan names as authorities (${(args.sources || []).join(', ')}) and compare every user-visible string, order, mnemonic, shortcut caption, dialog field and status field the wave added against them, character for character. Check the greyed items each state a fact. Check the About / product name / kernel bar name are the PRODUCT's, and that the About box says nothing about how the build renders. Check nothing invented was added "for convenience". Look at the screendumps in build/port-shots/ (Read the PNGs) and say whether what they show is what the original shows.` },
]

let reviews = []
let round = 0
let current = impl
while (true) {
  phase('Review')
  reviews = (await parallel(LENSES.map(l => () => agent(
`${READ_FIRST}

Wave ${WAVE} of the ${args.name} port has just been implemented; the implementer's report follows. Review the working tree READ-ONLY through one lens and try to find what is wrong. Be concrete: file, line, what, and the fix. Prefer few sharp findings. Do not edit anything, do not build, do not start qemu.

${l.prompt}

IMPLEMENTER'S REPORT:
${JSON.stringify(current, null, 1)}`,
    { label: `review:${l.key}:r${round}`, phase: 'Review', schema: REVIEW_SCHEMA })))).filter(Boolean)

  const findings = reviews.flatMap(r => r.findings.map(f => ({ ...f, lens: r.lens })))
  const serious = findings.filter(f => f.severity !== 'minor')
  log(`review round ${round}: ${findings.length} findings, ${serious.length} serious`)
  if (serious.length === 0 || round >= ROUNDS) break

  phase('Fix')
  const fixed = await agent(
`${READ_FIRST}

You are fixing wave ${WAVE} of the ${args.name} port after review. Apply every blocker and major finding below (and the minors that are cheap), rebuild through the gate, re-run the harness, re-boot and re-shoot what changed, and return the same report shape. If a finding is WRONG, say so in the report with the reason instead of applying it - do not apply a change you can show is a mistake. Do not commit.

${RULES}

FINDINGS:
${JSON.stringify(serious, null, 1)}

MINOR FINDINGS (apply if cheap):
${JSON.stringify(findings.filter(f => f.severity === 'minor'), null, 1)}

PREVIOUS REPORT:
${JSON.stringify(current, null, 1)}`,
    { label: `fix:wave${WAVE}:r${round}`, phase: 'Fix', schema: IMPL_SCHEMA })
  if (fixed) current = fixed
  round++
}

// ---------------------------------------------------------------- Verify
phase('Verify')
const verified = await agent(
`${READ_FIRST}

You are the independent verifier for wave ${WAVE} of the ${args.name} port. Do not trust the implementer's report: rebuild (the plan's make target - \`make ${args.name.toLowerCase()}\` unless it names another - and its disk target), run the host harness, kill any stale qemu (pkill -f qemu-system-i386), boot with \`make test TESTAPPS=build/<img>\`, double-click Disk B then the package, and check the plan's "done_when" for this wave with your own eyes: drive it with tools/mouse.py and tools/qmp.py, screendump with tools/shot.py --crop --zoom into build/port-shots/wave${WAVE}-verify-*.png, and Read each PNG. If the plan lists a done_when that is visible only on a 1bpp adapter, boot once with VIDEO=cga too (mouse.py --screen 640x200). Quit qemu when done. Report pass/fail with what you saw, and name any screendump that shows a defect.

IMPLEMENTER'S FINAL REPORT:
${JSON.stringify(current, null, 1)}`,
  { label: `verify:wave${WAVE}`, phase: 'Verify', schema: VERIFY_SCHEMA })

const status = current.status === 'blocked' ? 'blocked' : (verified && verified.pass ? 'done' : 'failed')
return {
  status,
  report: current.report + '\n\nVERIFIER: ' + (verified ? verified.report : '(no verifier report)'),
  questions: current.questions || [],
  size: current.size,
  shots: [...(current.shots || []), ...((verified && verified.shots) || [])],
  reviews,
}
