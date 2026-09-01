"""Commit-range Codex review and Claude validation pipeline."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from artifacts import render_summary, update_index, validate_artifacts
from reviewers import run_claude_validation, run_codex_review


ALLOWED_DIRTY = re.compile(
    r"^(?:\.claude/skills/(?:codex-code-review|claude-review-validator|review-pipeline)/|docs/code-review/|review-pipeline\.bat$)"
)


@dataclass(frozen=True)
class Context:
    repo_root: Path
    script_root: Path
    review_id: str
    base_commit: str
    head_commit: str
    branch: str
    review_root: Path
    review_path: Path
    validation_path: Path
    summary_path: Path
    manifest_path: Path
    log_path: Path

    def result(self) -> dict[str, object]:
        return {
            "review_id": self.review_id,
            "base_commit": self.base_commit,
            "head_commit": self.head_commit,
            "branch": self.branch,
            "artifacts": {
                "review": str(self.review_path),
                "validation": str(self.validation_path),
                "summary": str(self.summary_path),
                "manifest": str(self.manifest_path),
                "log": str(self.log_path),
            },
        }


class Logger:
    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._stream = path.open("a", encoding="utf-8", newline="\n")
        self.write(f"{'=' * 24} {datetime.now().astimezone().isoformat()} {'=' * 24}")

    def write(self, message: str) -> None:
        print(message, flush=True)
        self._stream.write(message + "\n")
        self._stream.flush()

    def close(self) -> None:
        self._stream.close()


def _git(repo: Path, *arguments: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        ["git", "-C", str(repo), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if check and completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(detail or f"git {' '.join(arguments)} failed with code {completed.returncode}")
    return completed


def _resolve_commit(repo: Path, revision: str) -> str:
    value = _git(repo, "rev-parse", "--verify", f"{revision}^{{commit}}").stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}", value):
        raise RuntimeError(f"Invalid commit: {revision}")
    return value


def build_context(base_revision: str, head_revision: str, review_date: str) -> Context:
    script_root = Path(__file__).resolve().parent
    repo_root = Path(_git(script_root, "rev-parse", "--show-toplevel").stdout.strip()).resolve()
    if not re.fullmatch(r"\d{8}", review_date):
        raise RuntimeError(f"Review date must use yyyyMMdd: {review_date}")
    base = _resolve_commit(repo_root, base_revision)
    head = _resolve_commit(repo_root, head_revision)
    branch = _git(repo_root, "branch", "--show-current").stdout.strip() or "(detached)"
    review_id = f"{review_date}-{base[:7]}-to-{head[:7]}"
    review_root = repo_root / "docs" / "code-review"
    return Context(
        repo_root=repo_root,
        script_root=script_root,
        review_id=review_id,
        base_commit=base,
        head_commit=head,
        branch=branch,
        review_root=review_root,
        review_path=review_root / "reviews" / f"review-{review_id}.md",
        validation_path=review_root / "validations" / f"validation-{review_id}.md",
        summary_path=review_root / "summaries" / f"summary-{review_id}.html",
        manifest_path=review_root / "manifests" / f"{review_id}.json",
        log_path=review_root / "logs" / f"pipeline-{review_id}.log",
    )


def preflight(context: Context, *, skip_clean_check: bool) -> None:
    ancestry = _git(
        context.repo_root,
        "merge-base",
        "--is-ancestor",
        context.base_commit,
        context.head_commit,
        check=False,
    )
    if ancestry.returncode:
        raise RuntimeError(f"Base commit is not an ancestor of head: {context.base_commit}..{context.head_commit}")
    checked_out_head = _resolve_commit(context.repo_root, "HEAD")
    if checked_out_head != context.head_commit:
        raise RuntimeError(f"Checked-out HEAD does not match target head: {checked_out_head} != {context.head_commit}")
    if skip_clean_check:
        return
    dirty: list[str] = []
    status = _git(context.repo_root, "status", "--porcelain=v1", "--untracked-files=all").stdout
    for line in status.splitlines():
        if len(line) < 4:
            continue
        path = line[3:].replace("\\", "/")
        if " -> " in path:
            path = path.rsplit(" -> ", 1)[1]
        if not ALLOWED_DIRTY.match(path):
            dirty.append(line)
    if dirty:
        raise RuntimeError("Uncommitted source/config changes detected:\n" + "\n".join(dirty))


def _archive_invalid(context: Context, path: Path, stage: str, log: Logger) -> None:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    destination = context.review_root / "logs" / f"failed-{stage}-{context.review_id}-{stamp}.md"
    path.replace(destination)
    log.write(f"Archived invalid {stage} artifact: {destination}")


def _validate_review(context: Context) -> None:
    validate_artifacts(
        context.review_path,
        review_id=context.review_id,
        base_commit=context.base_commit,
        head_commit=context.head_commit,
    )


def _validate_pair(context: Context) -> None:
    validate_artifacts(
        context.review_path,
        context.validation_path,
        review_id=context.review_id,
        base_commit=context.base_commit,
        head_commit=context.head_commit,
    )


def _relative(context: Context, path: Path) -> str:
    return path.relative_to(context.repo_root).as_posix()


def execute(context: Context, *, skip_clean_check: bool = False) -> dict[str, object]:
    for name in ("reviews", "validations", "summaries", "manifests", "logs"):
        (context.review_root / name).mkdir(parents=True, exist_ok=True)
    log = Logger(context.log_path)
    try:
        preflight(context, skip_clean_check=skip_clean_check)
        log.write(f"[STAGE] Pipeline started: {context.review_id}")
        if context.summary_path.exists():
            raise RuntimeError(f"Summary already exists: {context.summary_path}")
        if context.manifest_path.exists():
            raise RuntimeError(f"Manifest already exists: {context.manifest_path}")

        if context.review_path.exists():
            try:
                _validate_review(context)
                log.write(f"Artifact validation passed: {context.review_id}")
            except ValueError as error:
                log.write(f"[WARNING] {error}")
                _archive_invalid(context, context.review_path, "review", log)
        if not context.review_path.exists():
            log.write("[STAGE] Running Codex review")
            run_codex_review(
                repo_root=context.repo_root,
                review_id=context.review_id,
                base_commit=context.base_commit,
                head_commit=context.head_commit,
                branch=context.branch,
                output_path=context.review_path,
                log=log.write,
            )
        _validate_review(context)
        log.write(f"Artifact validation passed: {context.review_id}")

        if context.validation_path.exists():
            try:
                _validate_pair(context)
                log.write(f"Artifact validation passed: {context.review_id}")
            except ValueError as error:
                log.write(f"[WARNING] {error}")
                _archive_invalid(context, context.validation_path, "validation", log)
        if not context.validation_path.exists():
            log.write("[STAGE] Running Claude validation")
            run_claude_validation(
                repo_root=context.repo_root,
                review_id=context.review_id,
                base_commit=context.base_commit,
                head_commit=context.head_commit,
                branch=context.branch,
                review_path=context.review_path,
                output_path=context.validation_path,
                log=log.write,
            )
        _validate_pair(context)
        log.write(f"Artifact validation passed: {context.review_id}")

        log.write("[STAGE] Rendering integrated HTML summary")
        render_summary(
            context.review_path,
            context.validation_path,
            context.summary_path,
            context.script_root.parent / "assets" / "summary-template.html",
        )
        log.write(f"Rendered summary: {context.summary_path}")
        manifest = {
            "schema_version": 1,
            "review_id": context.review_id,
            "created_at": datetime.now().astimezone().isoformat(),
            "status": "complete",
            "base_commit": context.base_commit,
            "head_commit": context.head_commit,
            "branch": context.branch,
            "artifacts": {
                "review": _relative(context, context.review_path),
                "validation": _relative(context, context.validation_path),
                "summary": _relative(context, context.summary_path),
                "log": _relative(context, context.log_path),
            },
        }
        temp_manifest = context.manifest_path.with_suffix(".json.tmp")
        temp_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        temp_manifest.replace(context.manifest_path)
        update_index(context.review_root)
        log.write(f"Updated index: {context.review_root / 'index.html'}")
        log.write(f"[COMPLETE] Review pipeline completed: {context.review_id}")
        return context.result()
    except Exception as error:
        log.write(f"[FAILED] Review pipeline failed: {error}")
        raise
    finally:
        log.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the Codex/Claude review pipeline.")
    parser.add_argument("--base", required=True, help="Excluded Git comparison baseline")
    parser.add_argument("--head", default="HEAD", help="Included target commit")
    parser.add_argument("--review-date", default=datetime.now().strftime("%Y%m%d"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-clean-check", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        context = build_context(args.base, args.head, args.review_date)
        if args.dry_run:
            preflight(context, skip_clean_check=args.skip_clean_check)
            print(json.dumps(context.result(), ensure_ascii=False, indent=2))
            return 0
        result = execute(context, skip_clean_check=args.skip_clean_check)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except Exception as error:
        print(f"[FAILED] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
