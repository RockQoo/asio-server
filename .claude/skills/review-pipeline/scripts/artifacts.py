"""Review artifact parsing, validation, rendering, and index generation."""

from __future__ import annotations

import html
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


CATEGORIES = {"실제 결함", "개선 권장", "추가 확인 필요"}
SEVERITIES = {"Critical", "High", "Medium", "Low"}
VERDICTS = {"Confirmed", "Partially Confirmed", "Rejected", "Needs Runtime Test"}
REVIEW_FIELDS = ("구분", "심각도", "파일과 위치", "문제 설명", "발생 조건", "코드 근거", "수정 방향", "검증 방법")
VALIDATION_FIELDS = ("판정", "재검수 근거", "심각도 재평가", "권장 조치")


@dataclass(frozen=True)
class Entry:
    identifier: str
    fields: dict[str, str]


@dataclass(frozen=True)
class Artifact:
    path: Path
    text: str
    metadata: dict[str, str]


def read_artifact(path: Path) -> Artifact:
    if not path.is_file():
        raise ValueError(f"Artifact not found: {path}")
    text = path.read_text(encoding="utf-8-sig")
    match = re.match(r"\A---\r?\n(.*?)\r?\n---(?:\r?\n|\Z)", text, re.DOTALL)
    if not match:
        raise ValueError(f"Missing frontmatter: {path}")
    metadata: dict[str, str] = {}
    for line in match.group(1).splitlines():
        if ":" in line:
            name, value = line.split(":", 1)
            metadata[name.strip()] = value.strip()
    return Artifact(path=path, text=text, metadata=metadata)


def entries(text: str, prefix: str) -> list[Entry]:
    heading = re.compile(rf"^## ({re.escape(prefix)}-\d{{3}})\s*$", re.MULTILINE)
    matches = list(heading.finditer(text))
    result: list[Entry] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        section = text[match.end() : end]
        fields: dict[str, str] = {}
        for line in section.splitlines():
            field_match = re.match(r"^- ([^:]+):\s*(.*)$", line)
            if field_match:
                fields[field_match.group(1).strip()] = field_match.group(2).strip()
        result.append(Entry(match.group(1), fields))
    return result


def _assert_metadata(artifact: Artifact, reviewer: str, expected: dict[str, str]) -> None:
    required = ["review_id", "reviewer", "base_commit", "head_commit", "branch", "reviewed_at"]
    for name in required:
        if not artifact.metadata.get(name):
            raise ValueError(f"Missing metadata '{name}': {artifact.path}")
    if artifact.metadata["reviewer"] != reviewer:
        raise ValueError(f"Expected reviewer '{reviewer}': {artifact.path}")
    try:
        datetime.fromisoformat(artifact.metadata["reviewed_at"].replace("Z", "+00:00"))
    except ValueError as error:
        raise ValueError(f"Invalid reviewed_at timestamp: {artifact.path}") from error
    for name, value in expected.items():
        if value and artifact.metadata.get(name) != value:
            raise ValueError(f"{name} mismatch: {artifact.path}")


def _assert_unique(items: Iterable[Entry], path: Path) -> None:
    identifiers = [item.identifier for item in items]
    duplicates = sorted({item for item in identifiers if identifiers.count(item) > 1})
    if duplicates:
        raise ValueError(f"Duplicate finding IDs in {path}: {', '.join(duplicates)}")


def _assert_fields(entry: Entry, required: Iterable[str]) -> None:
    for field in required:
        if not entry.fields.get(field):
            raise ValueError(f"Missing or empty field '{field}' in {entry.identifier}")


def validate_artifacts(
    review_path: Path,
    validation_path: Path | None = None,
    *,
    review_id: str = "",
    base_commit: str = "",
    head_commit: str = "",
) -> tuple[Artifact, Artifact | None]:
    expected = {"review_id": review_id, "base_commit": base_commit, "head_commit": head_commit}
    review = read_artifact(review_path)
    _assert_metadata(review, "codex", expected)
    if review.metadata.get("scope") != "committed-range":
        raise ValueError(f"Invalid or missing review scope: {review_path}")
    if not review.metadata.get("dynamic_validation"):
        raise ValueError(f"Missing metadata 'dynamic_validation': {review_path}")

    review_entries = entries(review.text, "CR")
    _assert_unique(review_entries, review_path)
    if not review_entries and not re.search(r"^## No Findings\s*$", review.text, re.MULTILINE):
        raise ValueError(f"Review must contain CR findings or '## No Findings': {review_path}")
    for entry in review_entries:
        _assert_fields(entry, REVIEW_FIELDS)
        if entry.fields["구분"] not in CATEGORIES:
            raise ValueError(f"Invalid category in {entry.identifier}")
        if entry.fields["심각도"] not in SEVERITIES:
            raise ValueError(f"Invalid severity in {entry.identifier}")

    if validation_path is None:
        return review, None

    validation = read_artifact(validation_path)
    _assert_metadata(validation, "claude", expected)
    for name in ("review_id", "base_commit", "head_commit", "branch"):
        if validation.metadata.get(name) != review.metadata.get(name):
            raise ValueError(f"Review/validation metadata mismatch: {name}")

    validation_entries = entries(validation.text, "CR")
    _assert_unique(validation_entries, validation_path)
    review_ids = {entry.identifier for entry in review_entries}
    validation_ids = {entry.identifier for entry in validation_entries}
    missing = sorted(review_ids - validation_ids)
    unknown = sorted(validation_ids - review_ids)
    if missing:
        raise ValueError(f"Missing validator verdicts: {', '.join(missing)}")
    if unknown:
        raise ValueError(f"Unknown CR IDs in validation: {', '.join(unknown)}")
    for entry in validation_entries:
        _assert_fields(entry, VALIDATION_FIELDS)
        if entry.fields["판정"] not in VERDICTS:
            raise ValueError(f"Invalid verdict in {entry.identifier}")
        if entry.fields["심각도 재평가"] not in SEVERITIES | {"Not Applicable"}:
            raise ValueError(f"Invalid reassessed severity in {entry.identifier}")

    claude_entries = entries(validation.text, "CL")
    _assert_unique(claude_entries, validation_path)
    for entry in claude_entries:
        _assert_fields(entry, REVIEW_FIELDS)
        if entry.fields["구분"] not in CATEGORIES or entry.fields["심각도"] not in SEVERITIES:
            raise ValueError(f"Invalid Claude finding classification in {entry.identifier}")
    return review, validation


def _field_html(fields: dict[str, str], names: Iterable[str]) -> str:
    return "".join(
        f"<dt>{html.escape(name)}</dt><dd>{html.escape(fields[name])}</dd>"
        for name in names
        if name in fields
    )


def render_summary(review_path: Path, validation_path: Path, output_path: Path, template_path: Path) -> None:
    if output_path.exists():
        raise ValueError(f"Refusing to overwrite: {output_path}")
    review, validation = validate_artifacts(review_path, validation_path)
    assert validation is not None
    review_entries = entries(review.text, "CR")
    validation_by_id = {item.identifier: item.fields for item in entries(validation.text, "CR")}
    new_findings = entries(validation.text, "CL")

    severity_names = ("Critical", "High", "Medium", "Low")
    verdict_names = ("Confirmed", "Partially Confirmed", "Rejected", "Needs Runtime Test")
    severity_counts = {name: sum(item.fields.get("심각도") == name for item in review_entries) for name in severity_names}
    verdict_counts = {name: sum(item.get("판정") == name for item in validation_by_id.values()) for name in verdict_names}

    cards: list[str] = []
    for item in review_entries:
        severity = item.fields["심각도"]
        cards.append(
            f"<section class='finding {severity.lower()}'><h2>{html.escape(item.identifier)} · {html.escape(severity)}</h2>"
            f"<div class='grid'><div><h3>Codex review</h3><dl>{_field_html(item.fields, REVIEW_FIELDS)}</dl></div>"
            f"<div><h3>Claude validation</h3><dl>{_field_html(validation_by_id[item.identifier], VALIDATION_FIELDS)}</dl></div></div></section>"
        )
    for item in new_findings:
        severity = item.fields["심각도"]
        cards.append(
            f"<section class='finding {severity.lower()}'><h2>{html.escape(item.identifier)} · Claude new finding · {html.escape(severity)}</h2>"
            f"<dl>{_field_html(item.fields, REVIEW_FIELDS)}</dl></section>"
        )
    if not cards:
        cards.append("<section class='finding'><h2>No Findings</h2><p>두 검토 단계에서 보고된 finding이 없습니다.</p></section>")

    title = f"Code Review {review.metadata['review_id']}"
    count_html = "".join(
        f"<span><span class='label'>{name}</span> <span class='value'>{count}</span></span>"
        for name, count in [*severity_counts.items(), *verdict_counts.items()]
    )
    body = (
        f"<h1>{html.escape(title)}</h1>\n"
        f"<section class='meta'><div><div class='label'>Range</div><div class='value'>{html.escape(review.metadata['base_commit'])} → {html.escape(review.metadata['head_commit'])}</div></div>"
        f"<div><div class='label'>Branch</div><div class='value'>{html.escape(review.metadata['branch'])}</div></div>"
        f"<div><div class='label'>Artifacts</div><div><a href='../reviews/{html.escape(review_path.name)}'>Codex Markdown</a> · "
        f"<a href='../validations/{html.escape(validation_path.name)}'>Claude Markdown</a></div></div></section>\n"
        f"<section class='counts'>{count_html}</section>\n{''.join(cards)}"
    )
    template = template_path.read_text(encoding="utf-8-sig")
    rendered = template.replace("{{TITLE}}", html.escape(title)).replace("{{BODY}}", body)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(rendered, encoding="utf-8", newline="\n")


def update_index(review_root: Path) -> None:
    rows: list[str] = []
    for path in sorted((review_root / "manifests").glob("*.json"), reverse=True):
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
        summary_name = Path(manifest["artifacts"]["summary"]).name
        rows.append(
            f"<tr><td>{html.escape(manifest['review_id'])}</td><td><code>{html.escape(manifest['base_commit'][:7])}..{html.escape(manifest['head_commit'][:7])}</code></td>"
            f"<td>{html.escape(manifest['branch'])}</td><td><a href='summaries/{html.escape(summary_name)}'>open</a></td></tr>"
        )
    if not rows:
        rows.append('<tr><td colspan="4">No pipeline reports yet.</td></tr>')
    legacy = "".join(
        f"<li><a href='{html.escape(path.name)}'>{html.escape(path.stem)}</a></li>"
        for path in sorted(review_root.glob("review-*.html"))
    )
    legacy_html = f"<h2>Legacy reports</h2><ul>{legacy}</ul>" if legacy else ""
    content = (
        '<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Code Review Index</title><style>body{max-width:1000px;margin:40px auto;padding:0 20px;font:15px/1.5 system-ui,sans-serif}'
        'table{width:100%;border-collapse:collapse}th,td{padding:10px;border-bottom:1px solid #ccc;text-align:left}code{white-space:nowrap}</style></head>'
        f'<body><h1>Code Review Index</h1><table><thead><tr><th>Review ID</th><th>Range</th><th>Branch</th><th>Summary</th></tr></thead>'
        f'<tbody>{"".join(rows)}</tbody></table>{legacy_html}</body></html>\n'
    )
    (review_root / "index.html").write_text(content, encoding="utf-8", newline="\n")
