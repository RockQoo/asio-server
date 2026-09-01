"""Read-only Codex and Claude Code reviewer integrations."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Callable


Log = Callable[[str], None]


def _which_codex() -> str:
    command = shutil.which("codex.exe") or shutil.which("codex")
    if not command:
        raise RuntimeError("Codex CLI not found on PATH.")
    return command


def _which_claude() -> str:
    direct = shutil.which("claude.exe")
    if direct:
        return direct
    command = shutil.which("claude.cmd")
    if command:
        bundled = Path(command).parent / "node_modules" / "@anthropic-ai" / "claude-code" / "bin" / "claude.exe"
        if bundled.is_file():
            return str(bundled)
    raise RuntimeError("Claude Code executable not found on PATH.")


def _run(command: list[str], *, cwd: Path, log: Log, stdin: str | None = None) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        input=stdin.encode("utf-8") if stdin is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env={**os.environ, "PYTHONUTF8": "1"},
    )
    stdout = completed.stdout.decode("utf-8", errors="replace").strip()
    stderr = completed.stderr.decode("utf-8", errors="replace").strip()
    if stdout:
        log(stdout)
    if stderr:
        log(stderr)
    return completed


def run_codex_review(
    *,
    repo_root: Path,
    review_id: str,
    base_commit: str,
    head_commit: str,
    branch: str,
    output_path: Path,
    log: Log,
) -> None:
    if output_path.exists():
        raise RuntimeError(f"Refusing to overwrite: {output_path}")
    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    prompt = f"""독립 코드 리뷰를 수행하고 결과만 Markdown으로 출력하라. 파일을 절대 수정하지 마라.
review_id={review_id}
base_commit={base_commit}
head_commit={head_commit}
branch={branch}

반드시 .claude/skills/review-pipeline/references/review-contract.md를 읽고 그 형식을 정확히 지켜라.
AGENTS.md, README.md, CLAUDE.md, PROGRESS.md, .claude/rules와 현재 코드를 읽고 Initial commit부터 target HEAD까지의 흐름도 참고하라.
.git, .vs, bin, obj, logs, 3rd/asio, docs/code-review는 리뷰 대상에서 제외하라.
설명은 한국어로 작성하고 파일명, 클래스명, 함수명, 코드 식별자와 오류 메시지는 원문을 유지하라.
reviewed_at은 현재 ISO 8601 시각, scope는 committed-range, dynamic_validation은 실제 실행한 검증 또는 not-run으로 기록하라.
"""
    try:
        completed = _run(
            [
                _which_codex(),
                "-a",
                "never",
                "exec",
                "-C",
                str(repo_root),
                "-s",
                "read-only",
                "--ephemeral",
                "-o",
                str(temp_path),
                prompt,
            ],
            cwd=repo_root,
            log=log,
        )
        if completed.returncode:
            raise RuntimeError(f"Codex exited with code {completed.returncode}")
        if not temp_path.is_file() or temp_path.stat().st_size == 0:
            raise RuntimeError("Codex produced no review artifact.")
        temp_path.replace(output_path)
    finally:
        temp_path.unlink(missing_ok=True)


def _claude_schema() -> dict[str, object]:
    finding_properties = {
        "id": {"type": "string", "pattern": r"^CR-[0-9]{3}$"},
        "verdict": {"type": "string", "enum": ["Confirmed", "Partially Confirmed", "Rejected", "Needs Runtime Test"]},
        "evidence": {"type": "string", "minLength": 1},
        "severity": {"type": "string", "enum": ["Critical", "High", "Medium", "Low", "Not Applicable"]},
        "action": {"type": "string", "minLength": 1},
    }
    new_properties = {
        "id": {"type": "string", "pattern": r"^CL-[0-9]{3}$"},
        "category": {"type": "string", "enum": ["실제 결함", "개선 권장", "추가 확인 필요"]},
        "severity": {"type": "string", "enum": ["Critical", "High", "Medium", "Low"]},
        "location": {"type": "string", "minLength": 1},
        "problem": {"type": "string", "minLength": 1},
        "condition": {"type": "string", "minLength": 1},
        "evidence": {"type": "string", "minLength": 1},
        "direction": {"type": "string", "minLength": 1},
        "verification": {"type": "string", "minLength": 1},
    }
    return {
        "type": "object",
        "additionalProperties": False,
        "required": ["findings", "new_findings"],
        "properties": {
            "findings": {
                "type": "array",
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "required": ["id", "verdict", "evidence", "severity", "action"],
                    "properties": finding_properties,
                },
            },
            "new_findings": {
                "type": "array",
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "required": ["id", "category", "severity", "location", "problem", "condition", "evidence", "direction", "verification"],
                    "properties": new_properties,
                },
            },
        },
    }


def _one_line(value: object) -> str:
    return " ".join(str(value).split())


def run_claude_validation(
    *,
    repo_root: Path,
    review_id: str,
    base_commit: str,
    head_commit: str,
    branch: str,
    review_path: Path,
    output_path: Path,
    log: Log,
) -> None:
    if output_path.exists():
        raise RuntimeError(f"Refusing to overwrite: {output_path}")
    relative_review = review_path.relative_to(repo_root).as_posix()
    prompt = f"""Codex 리뷰를 독립적으로 재검수하라. 파일을 절대 수정하지 마라.
review_id={review_id}
base_commit={base_commit}
head_commit={head_commit}
branch={branch}
review_path={relative_review}

.claude/skills/review-pipeline/references/review-contract.md와 review_path의 리뷰를 읽어라.
각 CR 항목을 같은 커밋의 코드와 Git 이력으로 재검증하고 모든 CR ID를 findings에 정확히 한 번 포함하라.
새 결함만 CL-001부터 new_findings에 추가하라. 새 결함이 없으면 빈 배열을 반환하라.
.git, .vs, bin, obj, logs, 3rd/asio, docs/code-review의 생성 산출물은 검토 대상에서 제외하라.
설명은 한국어로 작성하고 파일명, 클래스명, 함수명, 코드 식별자와 오류 메시지는 원문을 유지하라.
Markdown이나 서문을 출력하지 말고 제공된 JSON schema만 반환하라.
"""
    schema = json.dumps(_claude_schema(), ensure_ascii=False, separators=(",", ":"))
    completed = _run(
        [
            _which_claude(),
            "-p",
            "--no-session-persistence",
            "--permission-mode",
            "plan",
            "--tools",
            "Read,Glob,Grep,Bash",
            "--output-format",
            "json",
            "--json-schema",
            schema,
        ],
        cwd=repo_root,
        log=log,
        stdin=prompt,
    )
    if completed.returncode:
        raise RuntimeError(f"Claude Code exited with code {completed.returncode}")
    try:
        envelope = json.loads(completed.stdout.decode("utf-8-sig"))
        data = envelope.get("structured_output")
        if data is None and envelope.get("result"):
            data = json.loads(envelope["result"])
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError) as error:
        raise RuntimeError(f"Invalid Claude JSON response: {error}") from error
    if not isinstance(data, dict):
        raise RuntimeError("Claude JSON response did not contain structured_output.")

    lines = [
        "---",
        f"review_id: {review_id}",
        "reviewer: claude",
        f"base_commit: {base_commit}",
        f"head_commit: {head_commit}",
        f"branch: {branch}",
        f"reviewed_at: {datetime.now().astimezone().isoformat()}",
        "---",
        "",
        "# Claude Review Validation",
        "",
    ]
    for finding in data.get("findings", []):
        lines.extend(
            [
                f"## {_one_line(finding['id'])}",
                "",
                f"- 판정: {_one_line(finding['verdict'])}",
                f"- 재검수 근거: {_one_line(finding['evidence'])}",
                f"- 심각도 재평가: {_one_line(finding['severity'])}",
                f"- 권장 조치: {_one_line(finding['action'])}",
                "",
            ]
        )
    for finding in data.get("new_findings", []):
        lines.extend(
            [
                f"## {_one_line(finding['id'])}",
                "",
                f"- 구분: {_one_line(finding['category'])}",
                f"- 심각도: {_one_line(finding['severity'])}",
                f"- 파일과 위치: {_one_line(finding['location'])}",
                f"- 문제 설명: {_one_line(finding['problem'])}",
                f"- 발생 조건: {_one_line(finding['condition'])}",
                f"- 코드 근거: {_one_line(finding['evidence'])}",
                f"- 수정 방향: {_one_line(finding['direction'])}",
                f"- 검증 방법: {_one_line(finding['verification'])}",
                "",
            ]
        )
    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    try:
        temp_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
        temp_path.replace(output_path)
    finally:
        temp_path.unlink(missing_ok=True)
