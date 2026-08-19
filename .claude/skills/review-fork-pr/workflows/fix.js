// =============================================================================
// review-fork-pr / workflows/fix.js  --  THE FIX WORKFLOW
//
// Invoked by the review-fork-pr skill (SKILL.md, step 7), AFTER the user has
// approved the plan:
//
//   Workflow({ scriptPath: "<repo>/.claude/skills/review-fork-pr/workflows/fix.js",
//              args: { wt: "<abs path of the scratch worktree holding the PR>",
//                      pr: <PR number>,
//                      model: "Claude Opus 5 (1M context)",   // for Co-Authored-By
//                      batches: [ { key:   "B1-compactor-pointers",
//                                   title: "Pointers banked across a call that can move memory",
//                                   findings: [ <verified finding objects from review.js> ],
//                                   extra: "<batch-specific verification, commands and what they
//                                            must print>" }, ... ] } })
//
// Batches run SEQUENTIALLY on purpose: they share one worktree, and two agents
// editing one tree do not merge. Each fixer applies its batch, builds, runs its
// own verification and commits ONLY its own changes. No fixer ever pushes.
//
// Returns { reports[], applied, declined, failed[] }.
// =============================================================================
export const meta = {
  name: 'fix-fork-pr',
  description: 'Apply verified fork-PR review findings in sequential batches, each fixer verifying its own work',
  phases: [{ title: 'Fix', detail: 'one agent per batch, in order, each building and committing its own' }],
}

const WT = args.wt
const PR = args.pr
const MODEL = args.model || 'Claude Opus 5 (1M context)'
const BATCHES = args.batches || []
if (!BATCHES.length) throw new Error('fix.js: args.batches is empty')

const RULES = `You are fixing VERIFIED defects in pull request #${PR} of os8088 - a Macintosh System 1-style GUI
operating system for the Intel 8086/8088, written entirely in real-mode NASM assembly.

THE PR IS SOMEONE ELSE'S WORK, from a contributor's fork, and the repository owner has approved
exactly these fixes. That shapes everything below.

WORK IN THIS WORKTREE, and nowhere else:  ${WT}
Every edit, build and commit happens there. Do NOT push. Do NOT rebase, amend, squash or touch any
commit that is not your own.

FIRST read ${WT}/CLAUDE.md in full. Its hard rules bind your edits:
- 8086 only: no pusha/popa, no "push imm", no "shl reg,imm" other than 1, no movzx/movsx, no 32-bit
  registers.
- Near model: CS = DS = KERNEL_SEG for kernel code and every task; SS = LOW_SEG. SS != DS, so
  [bp+disp] addresses SS - kernel code holding a pointer in BP needs [ds:bp+...].
- Register discipline: public routines preserve every register but their documented outputs. ISRs
  push DS/ES, load DS = KERNEL_SEG, cld before string ops. Critical sections are pushf/cli ... popf,
  never cli/sti, and they must cover the operation's parameters as well as its body.
- Section discipline: a module that switches sections must switch back to .text before the file ends.
- Label hygiene: every module-internal label carries its module prefix or is a NASM local label.
- Three adapters, one binary: the live screen is [vid_w]/[vid_h]/[vid_stride], never the VGA
  reference constants. On 1bpp adapters grey rounds to black, so greying is a pattern through the pen.
- Package/driver boundary: callbacks are NEAR procs with a near ret, entered with ES = KERNEL_SEG, so
  a callback reads window fields as [es:bx+W_W], never [bx+W_W].
- SPEC.md is the binding contract and is updated BEFORE the change, not after. A bare "S" sign
  anywhere in the repo means SPEC.md. If your fix changes anything SPEC.md pins - a constant, a
  register contract, a struct offset, a slot number, an author rule - you MUST update SPEC.md in the
  same edit, in the section that already owns that rule.
- Performance: the target is a 4.77 MHz 8088. Any gfx_* call costs 756 us whatever it draws; one
  int 13h call costs ~400 ms whatever it moves. Do not add a redraw, a doubled draw, or a disk round
  trip. A redraw is priced by how many primitive calls it makes, not how many pixels it covers.
- NEVER relieve a guard to make something fit. Raising KERN_BUDGET or KERN_CODE_MAX is the repository
  owner's decision, not a build fix. Align a STALE symbol to the real value if that is the defect;
  widen an instrumented-build exemption if that is the defect; never move the shipped kernel's guard.

HOW TO WORK:
1. Your batch's findings are below. Each has already been reviewed and adversarially verified by a
   second agent: "reasoning" records how the defect was confirmed and "corrected_fix" is the fix that
   verifier settled on. It is usually precise enough to apply directly.
2. Before editing, open the cited file at the cited line and confirm the code really is as described.
3. Apply the corrected fix. Prefer it to inventing your own - it was derived with more context than
   you have. BUT you are not a typist: if the real code shows the prescribed fix is wrong, incomplete
   or would break something else, do the RIGHT thing and say in your report exactly what you did
   differently and why.
4. KEEP THE CONTRIBUTOR'S INTENT. Fix the hazard the way this design already handles that class of
   hazard. If their design makes something movable, guard the copy; do not pin it because pinning is
   easier to write.
5. If a finding turns out on inspection NOT to be a real defect, do NOT edit. Report it as declined,
   with your reasoning. A bad edit to working code is worse than a missed fix.
6. Keep every edit MINIMAL and surgical. No refactoring, no reformatting, no improving nearby code,
   nothing outside your batch. Match the surrounding style exactly: comment density, the alignment of
   the trailing comment column, capitalisation, and the way section references are written.

VERIFICATION IS PART OF THE JOB - a fix you have not verified is not done:
- "cd ${WT} && make" MUST succeed. It runs nasm with -w+error, so any 8086 violation or bad symbol
  fails the build, and it runs tools/checkdocs.py, so a stale citation fails it too.
- If you touched anything under kernel/ or the Makefile, ALSO run "cd ${WT} && make KERN_SMALL=1".
  Both kernels must assemble and both must stay under their size guards - a symbol that exists only
  in the big kernel is a real and recurring break.
- If a kernel size moved, run "python3 tools/kernsize.py --build build --bless" (and the small
  variant) in the same commit, as docs/KERNEL-MEMORY.md asks.
- Run every batch-specific verification listed in your assignment, and quote what it printed.
- If your build fails, fix it. Never report a batch whose build does not pass.

COMMIT WHEN DONE, and only then, in the worktree:
  cd ${WT} && git add -A && git commit
Write the message in this repo's style: one summary line naming the section or subsystem and what
changed, a blank line, then one bullet per fix saying what would have gone wrong. End with:
Co-Authored-By: ${MODEL} <noreply@anthropic.com>
Commit ONLY your own batch's changes.`

const FIX_SCHEMA = {
  type: 'object',
  required: ['batch', 'applied', 'declined', 'verification', 'build_ok', 'commit', 'notes'],
  properties: {
    batch: { type: 'string' },
    applied: {
      type: 'array',
      items: {
        type: 'object',
        required: ['title', 'file', 'what_changed', 'consequence', 'deviated'],
        properties: {
          title: { type: 'string' },
          file: { type: 'string' },
          what_changed: { type: 'string', description: 'plain description of the actual edit made' },
          consequence: { type: 'string', description: 'what would have gone wrong for a USER without this fix - one sentence, no jargon; the PR comment is written from this' },
          deviated: { type: 'string', description: 'empty if the prescribed fix was applied as-is; otherwise what you did differently and why' },
        },
      },
    },
    declined: {
      type: 'array',
      items: { type: 'object', required: ['title', 'reason'], properties: { title: { type: 'string' }, reason: { type: 'string' } } },
    },
    spec_updated: { type: 'string', description: 'which SPEC.md sections you edited, or empty if none needed' },
    verification: { type: 'string', description: 'exactly which commands you ran and what they printed' },
    build_ok: { type: 'boolean' },
    commit: { type: 'string', description: 'the commit sha you created, or empty if you made no changes' },
    notes: { type: 'string', description: 'anything the maintainer should know before pushing this to the contributor' },
  },
}

phase('Fix')
const reports = []
for (let i = 0; i < BATCHES.length; i++) {
  const b = BATCHES[i]
  log(`batch ${i + 1}/${BATCHES.length}: ${b.key} (${(b.findings || []).length} findings)`)
  const r = await agent(
`${RULES}

YOUR BATCH: ${b.key} - ${b.title}

${b.extra || ''}

THE FINDINGS, already verified:
${JSON.stringify(b.findings || [], null, 1)}`,
    { label: `fix:${b.key}`, phase: 'Fix', schema: FIX_SCHEMA })

  if (!r) { log(`batch ${b.key}: the fixer returned nothing - it is NOT applied`); reports.push({ batch: b.key, failed: true }); continue }
  if (!r.build_ok) log(`batch ${b.key}: BUILD NOT OK - read this report before pushing anything`)
  reports.push(r)
}

const ok = reports.filter(r => r && !r.failed)
const applied = ok.flatMap(r => r.applied || [])
const declined = ok.flatMap(r => r.declined || [])
const failed = reports.filter(r => r && (r.failed || r.build_ok === false)).map(r => r.batch)
log(`fix: ${applied.length} applied, ${declined.length} declined, ${failed.length} batches needing attention`)

return { reports: ok, applied, declined, failed }
