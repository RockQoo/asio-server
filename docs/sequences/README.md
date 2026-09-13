# 패킷 시퀀스 다이어그램 규칙

대표 기능이 **어느 스레드를 거쳐 어디까지 가는지**를 패킷 단위로 따라가는 HTML 모음이다.
코드 설명(`CLAUDE.md`)이나 설계 근거(`docs/design/`)와 달리, 여기는 **흐름 하나를 끝까지**
보여주는 것만 한다.

## 새 흐름을 추가할 때

1. `../assets/style.css`를 그대로 `<link>`한다 — 새 CSS를 만들지 않는다.
2. `index.html`의 카드 목록에 링크를 추가한다.
3. **외부 리소스(CDN JS/폰트/이미지)를 쓰지 않는다.** 오프라인에서 더블클릭으로 여는
   로컬 문서다. Mermaid 같은 라이브러리를 쓰려면 벤더링부터 하고, 아니면 지금처럼
   순수 HTML/CSS로 그린다.

## 다이어그램 마크업

레인 N개를 **2N개의 서브 컬럼**으로 깐다. 레인 `i`의 중심선이 곧 그리드 라인 `2i`라서
화살표를 "중심에서 중심까지" 정확히 그을 수 있다.

```html
<div class="seq-head" style="grid-template-columns: repeat(6, 1fr);"> ... 레인 헤더 6개 ... </div>

<div class="seq-body" style="grid-template-columns: repeat(12, 1fr);">
    <div class="lifelines" style="grid-template-columns: repeat(6, 1fr);"><i></i>…6개…</div>

    <!-- 레인 2 -> 레인 4 : grid-column 4 / 8 -->
    <div class="msg m-world" style="grid-column: 4 / 8; grid-row: 3;">
        <span class="lbl">W2ZRelay</span>
        <span class="line"></span>
        <span class="desc">부연 설명(선택)</span>
    </div>
</div>
```

- `grid-row`는 **항상 명시한다** — 자동 배치에 맡기면 순서가 어긋난다.
- 오른쪽 → 왼쪽 화살표는 `grid-column`을 작은 쪽부터 쓰고 `rtl` 클래스를 붙인다.
- 화살표 색 클래스: `m-io` / `m-world` / `m-lb` / `m-player` / `m-zone` / `m-db`
  (보낸 쪽 스레드 색).
- 한 레인 안에서 벌어지는 일은 `.act` 박스. 실패/중단은 `.act.stop`,
  미구현은 `.act.todo`.

## 왜 플로우차트에서 시퀀스로 바꿨나

이전에는 `docs/flowcharts/`에 단계를 위에서 아래로 나열하는 방식이었는데, **누가 누구에게
보냈는지**가 드러나지 않았다. 이 프로젝트에서 핵심은 "패킷이 어느 스레드에서 어느 스레드로
넘어가는가"이고, 그건 행위자를 세로선으로 세워야 보인다.
