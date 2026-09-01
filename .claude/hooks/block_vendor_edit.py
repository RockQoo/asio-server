#!/usr/bin/env python
"""PreToolUse 훅: 3rd/asio(벤더 코드) 수정 시도를 자동으로 차단한다.

CLAUDE.md의 "3rd/asio(벤더 코드)는 절대 수정하지 않는다" 규칙을 기계적으로 강제한다.
Edit/Write/MultiEdit/NotebookEdit의 tool_input에서 file_path(들)를 뽑아 3rd/asio 아래인지 검사한다.
"""

import json
import sys


def collect_paths(tool_input: dict) -> list[str]:
    paths = []
    if "file_path" in tool_input:
        paths.append(tool_input["file_path"])
    for edit in tool_input.get("edits", []) or []:
        if isinstance(edit, dict) and "file_path" in edit:
            paths.append(edit["file_path"])
    return paths


def is_vendor_path(path: str) -> bool:
    normalized = path.replace("\\", "/").lower()
    return "/3rd/asio/" in normalized or normalized.endswith("/3rd/asio")


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0

    tool_input = payload.get("tool_input", {}) or {}
    blocked = [p for p in collect_paths(tool_input) if is_vendor_path(p)]

    if blocked:
        for path in blocked:
            print(
                f"[block-vendor-edit] '{path}'는 3rd/asio 벤더 코드라 수정이 차단됐다. "
                "CLAUDE.md 규칙: 3rd/asio는 절대 수정하지 않는다.",
                file=sys.stderr,
            )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())
