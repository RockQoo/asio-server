---
paths:
  - ".claude/**/*.md"
  - "**/*.md"
---

# MD Patterns

## 파일명 규칙

`.claude/` 아래 마크다운은 **kebab-case**(`cpp-patterns.md`, 한글도 하이픈 구분 허용) —
언더스코어/camelCase/PascalCase 금지, 확장자는 소문자 `.md`.

**예외(대문자 고정명)**: `CLAUDE.md`(루트, Claude Code 진입점 규약), `README.md`(루트, 관례),
`PROGRESS.md`(루트, 이 프로젝트의 구현 이력 문서), `SKILL.md`(`.claude/skills/*/`, 스킬
진입점 고정 파일명).

## 권장 구조

`.claude/rules/` 문서는 아래처럼 `paths:` front matter로 적용 대상을 명시한다(가독성/툴링용,
강제 파싱 아님):

```markdown
---
paths:
  - "**/*.{h,cpp}"
---
```

## 이유

`CLAUDE.md`/`PROGRESS.md`는 이미 확정된 문서라 그대로 두고, 앞으로 `.claude/rules/`,
`.claude/skills/`에 새 문서를 추가할 때 파일명이 들쭉날쭉해지는 것을 막기 위함이다.
