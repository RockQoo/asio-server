---
name: claude-review-validator
description: Re-check a versioned Codex review against the same committed source range with Claude Code, recording confirmation, rejection, severity changes, and newly discovered findings. Use after codex-code-review and before producing the HTML summary.
---

# Claude Review Validator

Validate, rather than summarize, every Codex finding.

1. Read `../review-pipeline/references/review-contract.md` completely.
2. Confirm the review metadata matches the requested `review_id`, `base_commit`, and `head_commit`.
3. Inspect the committed source evidence independently. Do not treat the Codex conclusion as authoritative.
4. Assign exactly one verdict to every `CR-*` finding: `Confirmed`, `Partially Confirmed`, `Rejected`, or `Needs Runtime Test`.
5. Record concrete evidence and a severity reassessment. Add independently discovered findings as `CL-*` entries using the full review fields.
6. Let `../review-pipeline/scripts/review_pipeline.py` invoke the Claude integration in `reviewers.py`; Claude Code must run without edit tools.
7. Run `artifacts.validate_artifacts()` and fail if any Codex finding lacks a validator verdict.

Write the artifact in Korean while preserving code identifiers and messages verbatim. Never modify source code or overwrite an existing validation.
