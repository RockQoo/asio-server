# Review Artifact Contract

## Identity and paths

Derive `review_id` as `yyyyMMdd-<base7>-to-<head7>` and use it unchanged:

- `docs/code-review/reviews/review-<review_id>.md`
- `docs/code-review/validations/validation-<review_id>.md`
- `docs/code-review/summaries/summary-<review_id>.html`
- `docs/code-review/manifests/<review_id>.json`

Use full 40-character hashes in metadata. Treat artifacts as immutable.

## Review Markdown

Start with YAML-like frontmatter containing `review_id`, `reviewer: codex`, `base_commit`, `head_commit`, `branch`, `reviewed_at`, `scope: committed-range`, and `dynamic_validation`.

Use `## CR-001` identifiers in ascending order. Every finding must contain these one-line fields:

- `구분`: `실제 결함`, `개선 권장`, or `추가 확인 필요`
- `심각도`: `Critical`, `High`, `Medium`, or `Low`
- `파일과 위치`
- `문제 설명`
- `발생 조건`
- `코드 근거`
- `수정 방향`
- `검증 방법`

If no findings exist, include `## No Findings` and a short explanation.

## Validation Markdown

Start with matching identity fields and `reviewer: claude`. For every `CR-*` identifier, include:

- `판정`: `Confirmed`, `Partially Confirmed`, `Rejected`, or `Needs Runtime Test`
- `재검수 근거`
- `심각도 재평가`: a valid severity or `Not Applicable`
- `권장 조치`

Add new findings as `CL-001` onward using all review fields. Never remove or renumber a Codex finding.

## Review scope

Read `AGENTS.md`, `README.md`, `CLAUDE.md`, `PROGRESS.md`, `.claude/rules/`, the current source, and relevant history from the initial commit through the target head. Exclude `.git`, `.vs`, `bin`, `obj`, `logs`, `3rd/asio`, and generated `docs/code-review` artifacts.

Prioritize async object/session lifetime, raw `this` captures, `shared_ptr`/`shared_from_this`, strand or serialization guarantees, races/deadlocks, shutdown with outstanding operations, packet/input validation, duplicate session registration/removal, Gateway/World/Zone responsibility boundaries, error handling/exception safety, and missing tests.
