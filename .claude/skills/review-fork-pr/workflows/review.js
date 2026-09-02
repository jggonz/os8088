// =============================================================================
// review-fork-pr / workflows/review.js  --  THE REVIEW WORKFLOW
//
// Invoked by the review-fork-pr skill (SKILL.md, step 5) through the Workflow
// tool:
//
//   Workflow({ scriptPath: "<repo>/.claude/skills/review-fork-pr/workflows/review.js",
//              args: { wt:   "<abs path of the scratch worktree holding the PR>",
//                      pr:   <PR number>,
//                      base: "<merge-base sha, or the sha to diff against>",
//                      emphasis: ["memory","regression","redraw","disk","abi"],
//                      units: [ { key:  "kernel-core",
//                                 title:"Kernel core, memory ladder, scheduler, loader",
//                                 paths:"kernel/kernel.asm kernel/memory.inc ...",
//                                 focus:"<what this reviewer must look at, in detail>" },
//                               ... ] } })
//
// One reviewer per unit, then one adversarial verifier per unit that tries to
// REFUTE that unit's findings. Nothing here edits the tree - every agent is
// read-only, and the skill (not this workflow) decides what gets fixed.
//
// Returns { confirmed[], uncertain[], refuted, summaries[], units }.
//
// Units are the caller's job: split by SUBSYSTEM and by LENS, not by file
// count. Ten is the cap here; a PR that wants more wants two runs.
// =============================================================================
export const meta = {
  name: 'review-fork-pr',
  description: 'Review an incoming fork PR unit by unit, then adversarially verify every finding',
  phases: [
    { title: 'Review', detail: 'one read-only reviewer per logical unit of the PR' },
    { title: 'Verify', detail: 'one adversary per unit, trying to refute each finding' },
  ],
}

const WT = args.wt
const PR = args.pr
const BASE = args.base || 'main'
const EMPH = (args.emphasis && args.emphasis.length) ? args.emphasis : ['memory', 'regression', 'redraw', 'disk', 'abi']
const UNITS = (args.units || []).slice(0, 10)
if ((args.units || []).length > 10) log(`review: only the first 10 of ${args.units.length} units are reviewed - split the rest into a second run`)
if (!UNITS.length) throw new Error('review.js: args.units is empty - the caller must split the PR into units')

const EMPHASIS_TEXT = {
  memory: 'MEMORY SAFETY: a pointer or segment banked across anything that can move, free or reallocate memory (a claim, a free, a yield, a callback, a compaction); a buffer written past its end; state not cleared when a slot is reused; a wait that does not actually wait for the thing it names.',
  regression: 'LOST-FROM-MAIN REGRESSIONS: this branch is long-lived, and a guard main added after the fork point is simply ABSENT here with no conflict reported. For every hunk that DELETES or rewrites something, run "git -C ' + WT + ' log main --oneline -- <file>" and "git -C ' + WT + ' show main:<file>" and ask what the deleted code was guarding.',
  redraw: 'REDRAW AND CPU COST against the 4.77 MHz target: an added full repaint, a doubled draw, a per-pixel primitive call, an int 13h call inside a loop. A redraw is priced by how many primitive calls it makes, not how many pixels it covers. Check PERFORMANCE.md Part 5 - if the operation is in that table, that row is the bar.',
  disk: 'DISK AND DATA INTEGRITY: a failure that does not propagate, a refusal reported with the wrong reason, a size or offset that does not fit its type, a name taken from outside the machine and used unvalidated, a disk-visible base that is not 512-byte aligned.',
  abi: 'THE PACKAGE/DRIVER ABI: an API slot whose number, contract or register answer changed without every caller changing with it; a driver verb table that grew without a header version bump; a callback that is not a near proc, or reads window fields without the ES override.',
}

const COMMON = `You are reviewing pull request #${PR} against os8088 - a Macintosh System 1-style GUI
operating system for the Intel 8086/8088, written entirely in real-mode NASM assembly, booted from
floppy. The PR comes from a contributor's FORK and is being prepared for merge into main.

A git worktree holding the PR (with main already merged into it, unless told otherwise) is at:
  ${WT}
Read files there. To see your unit's diff:   git -C ${WT} diff ${BASE} HEAD -- <paths>
To read main's version of a file:            git -C ${WT} show main:<path>
DO NOT MODIFY ANY FILE. This phase is read-only; the fixes are applied later, by someone else.

FIRST read ${WT}/CLAUDE.md in full - it states the project's hard rules and the performance model,
and both are binding. Then grep SPEC.md for the section numbers your code cites; SPEC.md is the
binding contract and is far too large to read end to end. A bare "S" sign anywhere in this repo
means SPEC.md.

THE HARD RULES. Violations are silent - they assemble cleanly and run wrong - and they are the
highest-value findings you can return:
- 8086 only: no pusha/popa, no "push imm", no "shl reg,imm" other than 1, no movzx/movsx, no 32-bit
  registers, no 0F-prefix instructions.
- Near model: CS = DS = KERNEL_SEG for kernel code and every task; SS = LOW_SEG. SS != DS, so
  [bp+disp] addresses SS - kernel code holding a pointer in BP needs [ds:bp+...].
- Register discipline: public routines preserve every register but their documented outputs. ISRs
  push DS/ES, load DS = KERNEL_SEG, cld before string ops. Critical sections are pushf/cli ... popf,
  never cli/sti - and the section must cover the operation's parameters, not just its body.
- Section discipline: a module that switches sections must switch back to .text before the file
  ends, or the next %include lands in the wrong section.
- Label hygiene: one flat namespace - every module-internal label carries its module prefix (vga_,
  wm_, menu_, fm_, dsk_, ...) or is a NASM local label.
- Three adapters, one binary: the live screen is [vid_w]/[vid_h]/[vid_stride], NOT the VGA reference
  constants SCREEN_W/SCREEN_H/ROW_BYTES. Anything that clips, centres or anchors to a screen edge
  must read the live values or it is wrong on two adapters of three. On 1bpp adapters grey rounds to
  black, so greying must be a pattern and must go through the pen.
- Every disk-visible base is 512-byte aligned: int 13h answers error 09h on a transfer straddling a
  64KB physical boundary.
- Package/driver boundary: calling out is a far call through an API cell that switches DS; calling in
  goes through the 3-byte dispatcher in the package header, so every callback is a NEAR proc with a
  near ret, entered with ES = KERNEL_SEG - a callback reads window fields as [es:bx+W_W], never
  [bx+W_W]. Missing the override assembles cleanly and reads the package's own image.
- Memory: KERN_BUDGET is the kernel's whole RAM footprint; KERN_CODE_MAX is .text + .bss inside one
  64KB window. Raising either is the repository owner's decision, never a build fix - so "raise the
  budget" is not a finding, and neither is a fix that relieves a guard.

THE PERFORMANCE MODEL. The target is an IBM PC/XT, 8088 at 4.77 MHz; the emulator you would test on
is ~1000x faster and is exact about work done, useless about time taken. Measured on a real 5150:
  any gfx_* drawing call, whatever it draws: 756 us FIXED      one 8x8 glyph cell: ~900 us
  one 78-cell row of text: ~71 ms                              an OSAPI_* far call: 46.7 us
  a near call+ret: 11 us      a full task switch: 693 us       system tick: 18.2 Hz
  one int 13h call, near enough whatever it moves: ~400 ms  (cost disk work in CALLS, not sectors)
A redraw is priced by how many primitive calls it makes, not by how many pixels it covers. Three
defects are invisible in an emulator and have cost this project bug after bug: a VISIBLE REDRAW, a
DOUBLE-DRAW FLASH, and INPUT OVERRUN.

WHAT THIS REVIEW WEIGHS HARDEST:
${EMPH.map(k => '- ' + (EMPHASIS_TEXT[k] || k)).join('\n')}

WHAT NOT TO REPORT: style preferences, comment wording, naming taste, "could be refactored",
anything speculative you cannot point at a specific line for, and anything you have not read the
code for. Do not report a design decision you merely disagree with - this is someone else's pull
request. It is far better to return 3 solid findings than 12 soft ones.

Ground every finding in code you actually read, and cite file and line in the POST-MERGE file.
Cap your output at 8 findings, ranked most severe first.`

const FINDINGS_SCHEMA = {
  type: 'object',
  required: ['unit', 'summary', 'findings'],
  properties: {
    unit: { type: 'string' },
    summary: { type: 'string', description: '2-4 sentences: what this unit changes and your overall read on it' },
    findings: {
      type: 'array', maxItems: 8,
      items: {
        type: 'object',
        required: ['title', 'file', 'severity', 'category', 'description', 'evidence', 'failure_scenario', 'suggested_fix', 'confidence'],
        properties: {
          title: { type: 'string', description: 'one line, <= 90 chars' },
          file: { type: 'string' },
          line: { type: 'integer' },
          severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low'] },
          category: { type: 'string', description: 'correctness | hard-rule | regression | performance | resource | abi | data-integrity' },
          description: { type: 'string', description: 'what is wrong and why it matters' },
          evidence: { type: 'string', description: 'the actual code lines or git output that prove it' },
          failure_scenario: { type: 'string', description: 'concrete: what a user does -> what goes wrong, on real hardware' },
          suggested_fix: { type: 'string', description: 'specific, minimal, in the shape the surrounding design already uses' },
          confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
        },
      },
    },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  required: ['verdicts'],
  properties: {
    verdicts: {
      type: 'array',
      items: {
        type: 'object',
        required: ['title', 'file', 'verdict', 'reasoning', 'severity', 'corrected_fix'],
        properties: {
          title: { type: 'string' },
          file: { type: 'string' },
          line: { type: 'integer' },
          verdict: { type: 'string', enum: ['CONFIRMED', 'REFUTED', 'UNCERTAIN'] },
          reasoning: { type: 'string', description: 'what you checked and what you found - name the lines you read' },
          severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low'], description: 'your severity, which may differ from the reporter s' },
          corrected_fix: { type: 'string', description: 'the fix, corrected if the reporter got it wrong; empty if REFUTED' },
        },
      },
    },
  },
}

// Pipeline, not a barrier: a unit's findings go to its adversary the moment
// that unit's reviewer is done, while the other units are still reading.
phase('Review')
const results = await pipeline(
  UNITS,
  u => agent(
`${COMMON}

YOUR UNIT: ${u.title}
PATHS: ${u.paths}

${u.focus || ''}

Start with:  git -C ${WT} diff ${BASE} HEAD -- ${u.paths}
Then read the post-merge files themselves for anything the diff does not make clear. Other reviewers
cover the other units of this PR - stay inside yours and do not duplicate them.`,
    { label: `review:${u.key}`, phase: 'Review', schema: FINDINGS_SCHEMA }),

  (review, u) => {
    if (!review || !review.findings || !review.findings.length) return { unit: u.key, review, verdicts: [] }
    return agent(
`${COMMON}

YOU ARE THE ADVERSARY. Another agent reviewed the "${u.title}" unit of PR #${PR} and returned the
findings below. Your job is to TRY TO REFUTE each one, not to agree with it. For every finding:

1. Open the cited file at the cited line in ${WT} and read the real code, plus enough of its callers
   and callees to judge it. The reporter may have misread, may have missed a guard elsewhere, or may
   be describing behaviour that main already had.
2. Check whether the "failure_scenario" can actually happen: is the path reachable, is the state it
   needs achievable, does an existing guard already prevent it?
3. Check whether it is NEW in this PR at all: "git -C ${WT} show main:<file>" and
   "git -C ${WT} log main --oneline -- <file>". A defect that predates the PR is REFUTED for the
   purposes of this review (say so in reasoning) - this PR is not the place to fix it.
4. Judge the suggested fix. If it is wrong, incomplete, or would break something else, write the
   corrected one, in the shape the surrounding design already uses.

Return CONFIRMED only for a defect you traced yourself. Return REFUTED when you can say why it
cannot happen. Return UNCERTAIN only when the answer needs hardware or a run you cannot do, and say
what would settle it. Default towards REFUTED when you genuinely cannot ground it - a false finding
costs a maintainer more than a missed one at this stage, because it becomes an edit to working code.

THE FINDINGS:
${JSON.stringify(review.findings, null, 1)}`,
      { label: `verify:${u.key}`, phase: 'Verify', schema: VERDICT_SCHEMA })
      .then(v => ({ unit: u.key, review, verdicts: (v && v.verdicts) || [] }))
  }
)

const ok = results.filter(Boolean)
const merged = ok.flatMap(r => {
  const byTitle = {}
  ;(r.review && r.review.findings || []).forEach(f => { byTitle[f.title] = f })
  return r.verdicts.map(v => Object.assign({}, byTitle[v.title] || {}, v, { unit: r.unit }))
})

const RANK = { critical: 0, high: 1, medium: 2, low: 3 }
const bySeverity = (a, b) => (RANK[a.severity] ?? 9) - (RANK[b.severity] ?? 9)
const confirmed = merged.filter(f => f.verdict === 'CONFIRMED').sort(bySeverity)
const uncertain = merged.filter(f => f.verdict === 'UNCERTAIN').sort(bySeverity)
const refuted = merged.filter(f => f.verdict === 'REFUTED').length

log(`review: ${UNITS.length} units, ${merged.length} findings judged - ${confirmed.length} confirmed, ${uncertain.length} uncertain, ${refuted} refuted`)

return {
  units: UNITS.map(u => u.key),
  summaries: ok.map(r => ({ unit: r.unit, summary: r.review && r.review.summary })),
  confirmed, uncertain, refuted,
}
