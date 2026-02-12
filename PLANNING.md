# Zinc — Planning

Read this document at the start of any planning session. The pre-flight checklist below is mandatory before any non-trivial implementation or history rebuild.

## Pre-flight checklist

Complete these steps **in order** before executing any implementation:

1. **Check `git status`.** Working tree must be clean. If dirty: commit or stash first. Never start a plan with uncommitted changes.
2. **Write the plan to `memory/rebuild-plan.md`** in the auto memory directory. Write it BEFORE any implementation step — not after, not "next," before. This is the only artifact that survives context compaction.
3. **Include an Alternatives Considered section** with at least two approaches and their trade-offs. Do not skip this even when a solution seems obvious.
4. **Include a verification checklist** with specific, runnable commands and expected output.
5. **Include a progress tracker** and update it as work proceeds.
6. **If the plan involves git history changes**, also read `REBUILD.md` and follow its additional pre-flight steps (backup tag, safety copies, rebuild script guards).

## Plan file requirements

The plan file (`memory/rebuild-plan.md`) must contain:

| Section | Contents |
|---------|----------|
| **Context** | What problem this solves and why |
| **Alternatives Considered** | 2+ approaches with trade-offs, and which was selected |
| **Changes** | Exact edits: old text → new text, not descriptions like "apply the fix" |
| **Verification checklist** | Runnable commands with expected output |
| **Progress** | Checkboxes updated as work proceeds |

For history rebuilds, also include:
- Source → target commit mapping
- Per-commit delta table (which files change at which commit)
- Fix-to-commit mapping with cross-file dependencies
- Full contents of any generated/variant files

## Key principles

- **Separate code changes from history surgery.** Implement and verify all code changes on HEAD first. Only then plan the history rewrite as a purely mechanical operation. Never mix the two.
- **Write everything to the plan file.** Context can compact at any time. The plan file is the only thing that survives. If it's not in the plan file, it doesn't exist.
- **If blocked, stop and discuss.** Never silently deviate from an approved plan. If something can't be executed as planned — due to unexpected complexity, lost work, or changed assumptions — stop and discuss with the user before proceeding.
- **Concrete over abstract.** "Apply the array fixes to codegen_expr.c" is not recoverable after compaction. "Replace lines 340-345 with this exact text" is.
