# 코드 리뷰 파이프라인 사용법

이 파이프라인은 지정한 Git 범위를 Codex가 리뷰하고, Claude Code가 결과를 재검증한 뒤 로컬 HTML 보고서를 생성한다. 리뷰 결과는 실행 환경마다 달라질 수 있으며 Git에는 커밋하지 않는다.

## 사전 준비

- 개발 변경 사항을 먼저 로컬 Git에 커밋한다. 원격 저장소로 push할 필요는 없다.
- `python`, `codex`, `claude` 명령을 `PATH`에서 실행할 수 있어야 한다.
- 비교할 BASE와 검토 대상 HEAD의 SHA-1을 확인한다.

```bat
git log --oneline
```

## 실행

저장소 루트에서 다음 파일을 실행한다.

```bat
review-pipeline.bat
```

프롬프트에 BASE SHA-1과 HEAD SHA-1을 입력한다. HEAD를 비우면 현재 `HEAD`를 사용한다. `BASE..HEAD`에서 BASE 커밋은 비교 기준이라 제외되고, HEAD 커밋은 리뷰에 포함된다. 사전 검사 결과가 맞으면 `Y`를 입력해 Codex 리뷰와 Claude 검수를 시작한다.

CLI에서 사전 검사만 하려면 다음 명령을 사용한다.

```bat
python .claude\skills\review-pipeline\scripts\review_pipeline.py --base <BASE> --head <HEAD> --dry-run
```

## 결과 확인

성공하면 콘솔에 `REVIEW PIPELINE COMPLETE`가 표시된다. 통합 보고서는 `docs/code-review/index.html`에서 열 수 있다. 세부 산출물은 다음 위치에 생성된다.

- `reviews/`: Codex 리뷰
- `validations/`: Claude 검수
- `summaries/`: 통합 HTML
- `manifests/`: 리뷰 범위와 실행 상태
- `logs/`: 실행 로그

같은 날짜와 커밋 범위의 완료 결과는 덮어쓰지 않는다. 다시 실행하려면 기존 로컬 산출물을 별도로 보관하거나 삭제해야 한다.
