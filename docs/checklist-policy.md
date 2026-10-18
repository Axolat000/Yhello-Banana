# Living daily-use checklist

The versioned working checklist is `docs/daily-use-checklist.json`. It is a planning tool that must evolve with evidence from development, upstream changes, tests and actual user needs. It is not an immutable specification or proof of implementation.

Workers should update it when they discover a worthwhile missing task, a better implementation, an obsolete assumption, a dependency, an oversized task or a justified priority change. Keep changes grounded in the goal of making Yhello-Banana usable. Do not add speculative features merely to keep agents busy.

For every new task include a stable unique `id`, `title`, `domain`, `priority` (`P0` through `P3`), `goal`, `acceptance`, `deliverable`, concrete `steps` and `tests`, `depends`, and an English `planningReason` explaining the evidence and benefit. Existing task descriptions may remain in their source language; new writing must be English.

For changed existing tasks, update `planningReason`. Keep historical entries and IDs. For an unstarted task that is no longer relevant, set `planningStatus` to `deferred` or `superseded`, explain why, and optionally list successor IDs in `replacedBy`. Adjust dependencies so active tasks do not wait forever on unfinished retired tasks. Do not delete history or claim completion by editing planning data.

Never rewrite the steps or retire a task whose execution has already started. Create a linked follow-up task instead, preserving existing work and the supervisor's execution state. Changing priority, evidence, acceptance criteria or documentation must not conceal unfinished obligations or weaken the actual tests.

Include planning edits in your worktree and commit for independent review. A checklist edit alone does not complete the assigned OS implementation step. If the implementation is blocked, preserve the proposal and report it rather than claiming ready for a different deliverable. Read-only agents suggest necessary planning changes in their findings/rationale; implementation workers apply them.

The reviewer evaluates usefulness, scope, alternatives, priority, dependencies and validation criteria. The supervisor validates the merged checklist structure and preserves started steps/history, then runs the usual integration checks. Accepted planning changes become available to the scheduler after integration. Invalid or unreviewed edits do not change scheduled work. External providers, quotas, server permissions, main-merge policy and automation rules are never changed through this checklist.

The older local HTML checklist is a snapshot; it does not synchronize itself while the user's PC is off. The JSON in this repository and the supervisor's execution state are the working references. Planning changes are visible in the normal integration PR.
