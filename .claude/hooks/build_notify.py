#!/usr/bin/env python
"""PostToolUse 훅: Bash로 MSBuild를 돌린 경우 성공/실패를 요약해서 알려준다.

MSBuild 출력은 한글 로케일이라 메시지 자체는 한글이지만, 에러 코드(C2061 등)는 로케일과
무관하게 항상 "error C####"/"error LNK####" 형식으로 나온다 - 이걸로 성공/실패를 판정한다.
MSBuild와 무관한 Bash 호출이면 아무 것도 하지 않고 조용히 종료한다.
"""

import json
import re
import sys

ERROR_PATTERN = re.compile(r"error [A-Z]{1,4}\d+", re.IGNORECASE)


def extract_text(value) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        return "\n".join(extract_text(v) for v in value.values())
    if isinstance(value, list):
        return "\n".join(extract_text(v) for v in value)
    return ""


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0

    command = (payload.get("tool_input", {}) or {}).get("command", "") or ""
    if "msbuild" not in command.lower():
        return 0

    output = extract_text(payload.get("tool_response", {}))
    errors = sorted(set(ERROR_PATTERN.findall(output)))

    if errors:
        preview = ", ".join(errors[:5])
        more = f" 외 {len(errors) - 5}건" if len(errors) > 5 else ""
        print(
            f"[build-notify] MSBuild 빌드 실패 — {len(errors)}건 에러 ({preview}{more}). "
            "위 Bash 출력에서 상세 내용을 확인하고 고칠 것.",
            file=sys.stderr,
        )
        return 2

    print("[build-notify] MSBuild 빌드 성공.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
