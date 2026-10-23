# Shared skills in Claude Code and Codex

The eight skills in `.claude/skills/` are the maintained sources, including
their lessons, workflow scripts and release helpers. `.agents/skills/` contains
relative symlinks to those same directories for Codex discovery. Edit the
sources; when adding a skill, add its discovery link too.

Claude Code invokes them as `/skill-name`; Codex CLI and the IDE use
`$skill-name` or `/skills`. Both can select a skill by its description, except
`vga-face`, which is explicitly invoked only. Its Claude frontmatter and
`agents/openai.yaml` express the same policy for the two hosts.

Read the selected `SKILL.md` and the lessons it names. Use `CLAUDE.md` in
Claude Code and `AGENTS.md` in Codex for the repository brief; they carry the
same rules. Interpret the host-specific notation below through this guide.
The user's scope, previous answers and authorization still apply: changing
hosts does not add an approval step or remove a skill's existing approval gate.

## Tools and session state

| Notation in a skill | Claude Code | Codex |
|---|---|---|
| `Workflow({ scriptPath, args })` | Use `Workflow` when provided by the session. | Follow the workflow with native agent tools as described below. |
| `Agent`, `agent(prompt, options)` | Use the session's agent tool. | Use `spawn_agent`, then the available message, wait and completion tools. Some hosts expose these under `collaboration`. |
| `AskUserQuestion` | Use the question tool. | Use an available question tool, or ask in chat when none is available. Respect its question/option limits; split a larger intake into smaller calls. |
| `Read` on text or an image | Use the file/image reader. | Read text through the shell; inspect PNGs with `view_image` or the host's image viewer. A filename or successful screenshot command is not visual evidence. |
| `Bash`, `run_in_background` | Use the shell's foreground/background execution. | Use the shell tool and its running-session handle; poll or resume that handle until completion and check the exit status. |
| `$ARGUMENTS`, `$1` | The invocation supplies these. | Take the package, PR, version and flags from the user's prompt. These names are notation, not environment variables to execute. |

Use answers already supplied in the conversation before asking intake
questions. If an answer is required, continue independent work while waiting,
but do not begin the dependent step without the answer. A timeout or a
preselected recommendation is not approval.

Use a session temporary directory when the host has no scratchpad. Keep
emulator worktree and socket paths short. Resolve repository and reference
paths from the actual checkout and invocation arguments, including when using
a discovery symlink; example paths and old personal paths are not defaults.
Stop only an emulator owned by this run, through its QMP socket or recorded
PID. Other worktrees may be running their own VMs.

When a skill asks to write a memory, use the host's memory facility if one is
available. Otherwise record the reference paths and decisions in a session
note outside the repository and include that note's location in the handoff.
Keep the portable decisions in the port plan; do not commit private host
paths just to replace Claude's memory. Use the actual session's attribution
when configured; never invent an agent name, model or email address.

## Running a workflow without the Workflow tool

The JavaScript under each skill's `workflows/` defines the prompts, schemas,
dependencies, review lenses, repair limits and result aggregation. It uses
host-provided globals (`args`, `agent`, `parallel`, `pipeline`, `phase`, `log`)
and is not a standalone Node command. Keep these scripts as the shared source
of the workflow; do not maintain a second set of Codex prompts.

1. Read the whole selected script, its skill and required lessons. Bind its
   `args` to the real worktree, source paths, user decisions and output paths.
   Resolve the prompt templates with those values. Pass each child its
   resolved prompt, the repository brief and this runtime guide; do not assume
   a child inherited the parent's context or tool names.
2. Use a fresh agent for each independent role. Inherit the session model;
   do not supply model overrides. For `parallel`, launch independent jobs up
   to the host's agent limit and queue the rest. A completed job must release
   its slot (close it if the host requires that) before starting another.
   With one child slot, run the roles sequentially with fresh agents.
3. For `pipeline`, send each completed review to a fresh adversary while other
   units continue. A barrier between these two stages is also valid when the
   host's scheduling requires it; keep the reviewer and adversary independent.
   Keep implementers, fixers, verifiers that rebuild, and committers sequential
   in their shared worktree. Read-only reviewers can run together and write
   only their own report files. Serialize updates to shared result/row files.
4. Collect every result in the script's schema, using JSON in a report file
   if the agent tool has no schema parameter. Validate required fields and
   statuses before passing a result downstream. Missing, malformed or failed
   agents are incomplete work, never an empty findings list or a passing
   verification. Retry or report the missing role before advancing.
5. Preserve the script's conditional fixes, repair/review limits and final
   return shape. Keep a session journal of role, agent ID, status and report
   path so a failure can be resumed without repeating successful stages.
   If a skill asks for `journal.jsonl`, use this journal on hosts that do not
   provide a Workflow journal. Do not advance past a failed required wave or
   claim completion with unresolved serious findings.
6. Preserve the testing boundary: each wave checks host builds and screenshots
   and names affected suite rows. Run their deduplicated soak union and
   `test-full` once after the final batch, as the repository brief requires.
   Report skips and unavailable capabilities explicitly.

Invoking `port-to-os8088`, `native-game-port` or `review-fork-pr` authorizes the
multi-agent work those skills describe on either host. If the host has no
delegation capability, report that before starting a required multi-agent
stage; a single agent cannot claim an independent review of its own work.
Missing `Workflow` alone is not a blocker when native delegation is available.

Use this stage map to locate the control flow in the shared scripts:

| Script | Required stages |
|---|---|
| `port-to-os8088/workflows/scout.js` | Source and tree scouts → draft planner → three adversarial lenses → reconciler; return plan, sources, tree and reviews. |
| `port-to-os8088/workflows/implement.js` | Implementer → three review lenses → bounded fix/review rounds → independent verifier; return status, questions, size, screenshots and reviews. |
| `native-game-port/workflows/port.js` | Five scouts → architect → sequential implementation/review/fix/verify waves with one repair round → completeness critic → final gate → performance audit. |
| `review-fork-pr/workflows/review.js` | One reviewer per unit → fresh adversary per unit with findings → aggregate confirmed, uncertain and refuted findings. |
| `review-fork-pr/workflows/fix.js` | Sequential approved batches; each fixer verifies and commits its own explicit paths, never pushes; aggregate applied, declined and failed batches. |

Discovery and invocation policy follow the
[Codex skills documentation](https://learn.chatgpt.com/docs/build-skills).
