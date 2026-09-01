---
name: review-pipeline
description: Orchestrate the repository's commit-based review workflow: preflight an exact Git range, run Codex review, run Claude validation, validate both artifacts, render a deterministic HTML summary, and update the review index. Use when the user asks to run or manage the complete review pipeline.
---

# Review Pipeline

Use one immutable commit range and one `review_id` across all stages.

1. Read `references/review-contract.md` completely.
2. Require development to be committed. Never create the development commit automatically.
3. Run `python scripts/review_pipeline.py --base <commit> --head <commit>` from the repository root, or use the interactive root `review-pipeline.bat`. Omit `--head` only when the current `HEAD` is the intended target.
4. Let the script stop on a dirty source tree, invalid ancestry, mismatched metadata, duplicate completed output, malformed findings, missing validator verdicts, or a failed reviewer process. Archive an invalid partial artifact under `docs/code-review/logs/` before retrying its stage.
5. Keep both reviewers read-only. The only permitted outputs are under `docs/code-review/`.
6. Report the generated review, validation, summary, manifest, and log paths. Do not claim dynamic verification unless a test command actually ran.

For interactive local use, run the repository-root `review-pipeline.bat`, paste the base and head SHA-1 values, review the dry-run output, and confirm execution. Treat `BASE..HEAD` as a Git range: `BASE` is the excluded comparison baseline and `HEAD` is the included target. To include a specific first commit, use its parent as `BASE`.

Use `--dry-run` to inspect the resolved range and output paths without invoking reviewers or writing files. Existing completed artifacts are immutable; choose a different range/date instead of overwriting them.
