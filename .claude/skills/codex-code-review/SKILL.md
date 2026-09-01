---
name: codex-code-review
description: Run an independent, read-only Codex review for an exact Git commit range and save the result as a versioned Markdown artifact. Use after development has been committed and before Claude validates the findings.
---

# Codex Code Review

Review only committed code in the requested `BASE..HEAD` range.

1. Read `../review-pipeline/references/review-contract.md` completely.
2. Confirm both revisions resolve to commits, `BASE` is an ancestor of `HEAD`, and the checked-out `HEAD` matches the requested head.
3. Refuse to run when source changes are uncommitted. Ignore generated review artifacts under `docs/code-review/` and pipeline files under `.claude/` for this check.
4. Let `../review-pipeline/scripts/review_pipeline.py` invoke the Codex integration in `reviewers.py` with the full commit hashes and output path.
5. Keep Codex in a read-only sandbox. Never allow the reviewer to edit repository files.
6. Validate the Markdown with `artifacts.validate_artifacts()` before reporting success.

Preserve filenames, class names, function names, identifiers, and error messages verbatim. Write explanations in Korean. Do not silently overwrite an existing artifact.
