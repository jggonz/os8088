// =============================================================================
// port-to-os8088 / workflows/scout.js  --  THE SCOUTING WORKFLOW
//
// Invoked by the port-to-os8088 skill (SKILL.md, step 4) through the Workflow
// tool:
//
//   Workflow({ scriptPath: "<repo>/.claude/skills/port-to-os8088/workflows/scout.js",
//              args: { repo: "<abs path of the os8088 checkout>",
//                      sources: ["<abs path of reference repo>", ...],
//                      app: "<what the user called the program>",
//                      name: "<proposed package name, <= 15 chars, or ''>",
//                      notes: "<anything the user said about scope>" } })
//
// It reads the reference source(s) and this tree, and returns a DRAFT PORT
// PLAN plus the list of decisions only the user can make. It writes nothing
// under apps/ - the plan file is written by the skill after the user has
// answered, so that a plan never records a decision nobody took.
//
// Under 15 agents: N source scouts (one per reference repo, capped at 4) +
// 3 tree scouts + 1 planner + 3 adversarial reviewers + 1 reconciler.
// =============================================================================
export const meta = {
  name: 'port-scout',
  description: 'Scout a reference codebase and the os8088 tree, and draft a port plan',
  phases: [
    { title: 'Scout', detail: 'the reference source(s) and the os8088 constraints, in parallel' },
    { title: 'Plan', detail: 'one planner drafts the port plan from every scout report' },
    { title: 'Verify', detail: 'three adversarial reviewers, then a reconciler folds their findings in' },
  ],
}

const REPO = args.repo
const SKILL = REPO + '/.claude/skills/port-to-os8088'
const SOURCES = (args.sources || []).slice(0, 4)
if ((args.sources || []).length > 4) log(`scout: only the first 4 of ${args.sources.length} sources are scouted; name fewer or merge them`)

const SOURCE_SCHEMA = {
  type: 'object',
  required: ['path', 'language', 'loc', 'summary', 'ui', 'formats', 'core', 'platform', 'license', 'authority_files'],
  properties: {
    path: { type: 'string' },
    language: { type: 'string', description: 'primary language(s) and dialect/toolchain, e.g. "K&R C + MASM, Windows 2.x API"' },
    loc: { type: 'integer', description: 'approximate lines of source, all languages' },
    summary: { type: 'string', description: 'what the program is and does, in one paragraph' },
    ui: { type: 'array', items: { type: 'object', required: ['element', 'file', 'detail'], properties: {
      element: { type: 'string', description: 'menu bar / a menu / a dialog / a toolbar / status line / keyboard map / about box / icon' },
      file: { type: 'string', description: 'the file that DEFINES it - the resource, table or string file, not a caller' },
      detail: { type: 'string', description: 'what it contains: every string, mnemonic, shortcut, field, in the order the source gives them' } } },
      description: 'every user-visible surface, each traced to the file that is the AUTHORITY on it' },
    formats: { type: 'array', items: { type: 'object', required: ['name', 'files', 'notes'], properties: {
      name: { type: 'string' }, files: { type: 'string' }, notes: { type: 'string', description: 'is it text or binary; is there a reader and a writer; how big is each; which subset a small port could carry' } } } },
    core: { type: 'array', items: { type: 'object', required: ['feature', 'files', 'loc', 'depends', 'portable'], properties: {
      feature: { type: 'string' }, files: { type: 'string' }, loc: { type: 'integer' },
      depends: { type: 'string', description: 'what it needs from the platform: floats, longs, malloc, printf, files, timers, threads, graphics' },
      portable: { type: 'string', enum: ['as-is', 'rewrite', 'no'], description: 'as-is: plain C the gate will pass; rewrite: the BEHAVIOUR ports, the code does not; no: needs something os8088 has not got' } } } },
    platform: { type: 'string', description: 'what the program assumes of its platform that os8088 does not offer (window system calls, pcode, DOS int 21h, printer, network...)' },
    license: { type: 'string', description: 'the license / copyright terms found in the tree, and what attribution they require. Say "none found" if none' },
    authority_files: { type: 'array', items: { type: 'string' }, description: 'the 5-15 files a porter must read verbatim, in priority order' },
  },
}

const TREE_SCHEMA = {
  type: 'object', required: ['topic', 'findings'],
  properties: { topic: { type: 'string' }, findings: { type: 'array', items: { type: 'object', required: ['fact', 'where'], properties: {
    fact: { type: 'string' }, where: { type: 'string', description: 'file:line or SPEC section' } } } } },
}

const PLAN_SCHEMA = {
  type: 'object',
  required: ['name', 'summary', 'authority', 'scope', 'files', 'budget', 'api_gaps', 'waves', 'verification', 'risks', 'questions'],
  properties: {
    name: { type: 'string', description: 'package name, <= 15 chars, upper case, not already in apps/' },
    summary: { type: 'string' },
    authority: { type: 'array', items: { type: 'object', required: ['surface', 'source'], properties: { surface: { type: 'string' }, source: { type: 'string' } } },
      description: 'for every user-visible surface, the reference file it is taken from - "taken from the source, not from memory"' },
    scope: { type: 'object', required: ['ships', 'greyed', 'absent'], properties: {
      ships: { type: 'array', items: { type: 'string' } },
      greyed: { type: 'array', items: { type: 'object', required: ['item', 'fact'], properties: { item: { type: 'string' }, fact: { type: 'string', description: 'the FACT the greying states (SPEC 47) - "no printer path in this OS", not "not done"' } } } },
      absent: { type: 'array', items: { type: 'string' }, description: 'left out entirely and why' } } },
    files: { type: 'array', items: { type: 'object', required: ['file', 'holds', 'resident'], properties: {
      file: { type: 'string', description: 'apps/<dir>/<x>.c, #included by the one translation unit' }, holds: { type: 'string' },
      resident: { type: 'boolean', description: 'true = keystroke path, stays in the package segment; false = ovl_* code' } } } },
    budget: { type: 'object', required: ['image_est', 'bss_est', 'ovl_est', 'basis'], properties: {
      image_est: { type: 'integer' }, bss_est: { type: 'integer' }, ovl_est: { type: 'integer' },
      basis: { type: 'string', description: 'how the estimate was made - lines x bytes/line from cword, buffers listed with sizes' } } },
    api_gaps: { type: 'array', items: { type: 'object', required: ['need', 'slot', 'action'], properties: {
      need: { type: 'string' }, slot: { type: 'string', description: 'the OSAPI slot or kernel facility, or "none exists"' },
      action: { type: 'string', description: 'add a thunk to os88thunk.asm + os88.h / write it in the package / grey the feature' } } } },
    waves: { type: 'array', items: { type: 'object', required: ['n', 'title', 'features', 'files', 'done_when'], properties: {
      n: { type: 'integer' }, title: { type: 'string' }, features: { type: 'array', items: { type: 'string' } },
      files: { type: 'array', items: { type: 'string' } },
      done_when: { type: 'string', description: 'what a screendump / the host harness shows when this wave is done' } } },
      description: 'implementation order: 1 = window + chrome + model, then the keystroke path, then commands, then file I/O, then dialogs. Each wave builds and boots on its own' },
    verification: { type: 'array', items: { type: 'string' }, description: 'the host harness to write, the QMP recipe, the 86Box machine, what to measure' },
    risks: { type: 'array', items: { type: 'string' } },
    questions: { type: 'array', items: { type: 'object', required: ['q', 'options', 'recommend', 'why'], properties: {
      q: { type: 'string' }, options: { type: 'array', items: { type: 'string' } }, recommend: { type: 'string' }, why: { type: 'string' } } },
      description: 'decisions only the user can make: name, scope cuts, file format, whether to add API slots, which reference wins when two disagree' },
  },
}

const REVIEW_SCHEMA = {
  type: 'object', required: ['lens', 'verdict', 'findings'],
  properties: { lens: { type: 'string' }, verdict: { type: 'string', enum: ['sound', 'fix', 'rethink'] },
    findings: { type: 'array', items: { type: 'object', required: ['severity', 'finding', 'fix'], properties: {
      severity: { type: 'string', enum: ['blocker', 'major', 'minor'] }, finding: { type: 'string' }, fix: { type: 'string' } } } } },
}

const READ_FIRST = `Read, in this order, before anything else:
  ${SKILL}/LESSONS.md            (what the CWORD port learned - binding)
  ${REPO}/docs/C-TOOLCHAIN.md    (the four C rules, the 60KB ceiling, the overlay)
  ${REPO}/apps/cc/os88.h         (the API a C package can reach; its header comment)
  ${REPO}/CLAUDE.md              (the hard rules and the performance table)`

// ---------------------------------------------------------------- Scout
phase('Scout')
const sourceReports = await parallel(SOURCES.map((src, i) => () => agent(
`You are scouting a reference codebase so that a program can be ported to os8088, an 8086 real-mode GUI OS written in assembly with a C toolchain (SmallerC, 16-bit, no linker, 60KB per package, no float/long/printf/malloc-as-you-know-it).

Reference source root: ${src}
The program: ${args.app}
The user's notes: ${args.notes || '(none)'}

Map the tree (ls -R, wc -l, grep) and READ the files that define what a user sees and touches: menu resources, key maps, dialog templates, string tables, status/tool bars, about text, icons; the file formats it reads and writes; the core algorithms; and its license/copyright. Distinguish the file that DEFINES a surface from the ones that merely use it - the porter will quote the defining file verbatim.

For each core feature say honestly whether the CODE can port to a strict 16-bit C subset (no long/float/double, no address-of-local, no struct by value, frames < 96 bytes, no string instructions, one translation unit) or whether only the BEHAVIOUR ports and it must be rewritten. Do not read every file - read the authorities and sample the rest. Do not write anything anywhere.`,
  { label: `scout:src${i + 1}`, phase: 'Scout', schema: SOURCE_SCHEMA })))

const TREE_TOPICS = [
  { key: 'api', prompt: `Report the API surface a C package can reach and what it cannot. Read ${REPO}/apps/cc/os88.h in full and ${REPO}/apps/cc/os88thunk.asm; list what is wrapped (windows, drawing, fonts/type library, files, menus, dialogs, clipboard, toast, timers, workers, sound, assoc), what is deliberately NOT wrapped and why, and how a new thunk is added (the pattern in os88thunk.asm, with one concrete example). Then read ${REPO}/apps/os88api.inc's slot list for facilities that exist in assembly but have no C thunk yet. Report facts with file:line.` },
  { key: 'precedent', prompt: `Report the PRECEDENTS a port should copy. Read the header comments (first ~200 lines) of ${REPO}/apps/cword/cword.c, ${REPO}/apps/cword/cwovl.c, ${REPO}/apps/cword/hosttest/cwuitest.c, ${REPO}/apps/cword/build.sh, ${REPO}/tests/covl/covl.c and ${REPO}/tests/chello/chello.c, and the cword and allapps sections of ${REPO}/Makefile (grep -n cword Makefile). Report: how a C package is split into #included files; the shim; the Makefile rules (CC_PACKAGE, extra prerequisites for #included files, disk rules in three geometries, the vm target); how the host harness stubs the API and what it checks; how the overlay is turned on and cut; how the glass shadow / damage model keeps a keystroke to a handful of calls; where package name, About box and kernel bar name come from. Facts with file:line.` },
  { key: 'limits', prompt: `Report the LIMITS a port must design against. Read ${REPO}/SPEC.md sections 73.5, 73.7, 73.8, 73.9, 73.11, 73.14 (grep -n '^### 70' SPEC.md for line numbers), ${REPO}/docs/KERNEL-MEMORY.md's summary, and the performance table in ${REPO}/CLAUDE.md. Report: APP_MAX_SIZE and how image/bss/sum are bounded; the frame cap and the stack sizes; the cost table (gfx call, glyph cell, OSAPI call, int 13h); the three emulator-invisible defects; the overlay's rules (only code moves, no address of ovl_*, refused load returns 0, worker tasks may not call it); the section-discipline and label-hygiene rules; SPEC 47 greying; the 12-row dialog limit on 640x200 adapters; SPEC 39's three adapters. Facts with file:line or section.` },
]
const treeReports = await parallel(TREE_TOPICS.map(t => () => agent(
`${READ_FIRST}\n\nThen: ${t.prompt}\n\nReturn structured facts only; you are briefing a planner who has not read the tree.`,
  { label: `scout:tree:${t.key}`, phase: 'Scout', schema: TREE_SCHEMA })))

const srcOk = sourceReports.filter(Boolean)
const treeOk = treeReports.filter(Boolean)
log(`scout: ${srcOk.length}/${SOURCES.length} source reports, ${treeOk.length}/${TREE_TOPICS.length} tree reports`)

// ---------------------------------------------------------------- Plan
phase('Plan')
const planPrompt = `${READ_FIRST}

You are drafting the PORT PLAN for bringing "${args.app}" to os8088 as a C package${args.name ? ` named ${args.name}` : ''}. The user's notes: ${args.notes || '(none)'}.

SOURCE SCOUT REPORTS (one per reference repo):
${JSON.stringify(srcOk, null, 1)}

OS8088 TREE REPORTS:
${JSON.stringify(treeOk, null, 1)}

Draft the plan. The standard is the CWORD port (SPEC.md 73.12): the user interface is the ORIGINAL's, taken from the source and not from memory - every menu string, mnemonic, shortcut, dialog field and status field traced to the file that defines it - and what cannot be carried is present and greyed with the FACT that greys it (SPEC 47), never silently dropped. The port is a REIMPLEMENTATION of behaviour in a strict C subset, not a compile of the old code: say which parts port as-is and which are rewritten.

Rules the plan must obey, each of which cost the CWORD port real time (LESSONS.md has the detail):
- ONE translation unit, split into #included .c files by concern; the shim and Makefile prerequisites name every included file.
- Budget from day one: estimate image + bss against 61,440 with cword's ratio (its 9,800 lines of C = ~36KB resident image + 18.5KB overlay + 24.5KB bss). Anything past ~50KB resident plans an overlay from the start, split by FREQUENCY (keystroke path resident, per-command code ovl_*), never by size alone.
- The redraw path is designed before a feature is written: a shadow of the glass, damage-only repaint, cost stated in calls and cells. A full repaint on every keystroke is a defect, not a first draft.
- Every buffer is static; the biggest ones are bss; frames stay small; no long/float; no struct by value; unsigned char for bytes.
- The 640x200 adapters bound every dialog to 12 rows and every layout reads the live screen size.
- A host harness (stubbed os88.h + a model of the glass) is planned as part of wave 1, not at the end.
- Verification is QMP screendumps cropped and zoomed, the host harness's cost table, and one 86Box period machine.
- Naming: the package, the source directory, the disk images and the vm dir must not collide with anything in apps/ or vm/, and must not answer to an existing program's name.
- License: name the attribution the reference requires and where it goes (file headers + About box).

Order the WAVES so that each builds and boots on its own: 1 = window, chrome, menu bar (from the source), the document/data model, the harness stub; 2 = the keystroke / interaction path with its damage model; 3 = the commands and their menus; 4 = file format in/out; 5 = dialogs and the rest; 6 = polish (About, icon, welcome document, disk images, vm machine, README/SPEC section). Fewer waves is fine for a small program.

Put in "questions" ONLY decisions the user has to make - the package name if unclear, scope cuts that change what ships, the file format if the original had several, whether to spend API slots, which reference wins where two disagree. Decide everything else yourself and record the decision in the plan.`
const draft = await agent(planPrompt, { label: 'plan:draft', phase: 'Plan', schema: PLAN_SCHEMA })

// ---------------------------------------------------------------- Verify
phase('Verify')
const LENSES = [
  { key: 'fit', prompt: 'THE FIT LENS. Will this fit and run on the target? Check the byte budget against the source LOC using cword\'s ratio; check every buffer named against bss; check that the wave-1 design has a damage model and does not full-repaint; check that anything on the keystroke path is resident and anything per-command is overlay-eligible; check for float/long/printf/malloc/recursion-heavy/struct-by-value assumptions in the "as-is" features; check the frame cap; check nothing plans a dialog over 12 rows or hard-codes 640x480. Refute the plan where it hand-waves.' },
  { key: 'fidelity', prompt: 'THE FIDELITY LENS. Is the UI the ORIGINAL\'s? For every surface in "authority", is the named source file really the definer? Is anything user-visible left to memory? Is every dropped feature present-and-greyed with a FACT, per SPEC 47, rather than missing? Are the keyboard collisions the BIOS imposes (Ctrl-H/I/M fold into BkSp/Tab/Enter; Ctrl+Space is a space) accounted for? Does the About box carry the product, the version, what this port is and the attribution - and nothing about how the build renders? Refute where the plan drifts from the source.' },
  { key: 'process', prompt: 'THE PROCESS LENS. Can this be built and verified in the order given? Does wave 1 boot? Does each wave name what a screendump shows when done? Is the host harness in wave 1? Are the Makefile prerequisites for #included files planned? Do names collide with apps/, vm/, build/ or an existing package? Is the license attribution placed? Are the questions for the user really the user\'s, and is anything left as a question that the plan should simply decide? Refute where a wave depends on a later one.' },
]
const reviews = await parallel(LENSES.map(l => () => agent(
`${READ_FIRST}\n\nA port plan has been drafted for "${args.app}". Review it adversarially through ONE lens and try to break it. Be concrete: name the section of the plan, the number, the file. Prefer few sharp findings to many soft ones. Default to "sound" only if you actually tried and failed to break it.\n\n${l.prompt}\n\nTHE PLAN:\n${JSON.stringify(draft, null, 1)}`,
  { label: `verify:${l.key}`, phase: 'Verify', schema: REVIEW_SCHEMA })))

const reviewsOk = reviews.filter(Boolean)
const blockers = reviewsOk.flatMap(r => r.findings.filter(f => f.severity === 'blocker'))
log(`verify: ${reviewsOk.length} reviews, ${blockers.length} blockers, ${reviewsOk.flatMap(r => r.findings).length} findings`)

const final = await agent(
`${READ_FIRST}\n\nYou are the reconciler. Fold these adversarial reviews into the draft port plan for "${args.app}": fix what is fixable in the plan itself, keep the plan's decisions unless a finding shows one wrong, and add to "questions" only what a reviewer showed is genuinely the user's call. Keep every field of the schema populated. Add a "risks" line for any major finding you could not resolve.\n\nDRAFT:\n${JSON.stringify(draft, null, 1)}\n\nREVIEWS:\n${JSON.stringify(reviewsOk, null, 1)}`,
  { label: 'plan:final', phase: 'Verify', schema: PLAN_SCHEMA })

return { plan: final || draft, sources: srcOk, tree: treeOk, reviews: reviewsOk }
