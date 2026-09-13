namespace Client;

/// <summary>
/// 클라이언트가 지금 어느 단계에 있는가. <see cref="ClientGame.Update"/>/<c>Draw</c>가 이
/// 값으로 통째로 갈린다 — 단계마다 화면도 다르고, 보내도 되는 패킷도 다르다.
///
/// <para>
/// <b>단계를 나눈 이유</b>: 서버가 로그인을 통과하기 전에는 게임 패킷을 존으로 넘기지 않는다
/// (<c>GatewayLinkHandler::HandleFromClient</c>). 그러니 이동/채팅/Echo를 쏘아봐야 조용히
/// 버려질 뿐이라, 클라이언트도 같은 경계를 갖고 있어야 "보냈는데 아무 일도 안 일어난다"가
/// 안 생긴다.
/// </para>
/// </summary>
public enum GamePhase
{
    /// <summary>ID/PW 입력. <b>서버가 떠 있든 아니든 이 화면부터 시작한다.</b></summary>
    Login,

    /// <summary>
    /// 로그인은 통과했고 <c>Z2CEnterZoneNotify</c>(존 배정)를 기다리는 중.
    ///
    /// <para>
    /// 지금 여기서 받는 것은 존 배정 하나뿐이라 대개 한순간에 지나간다. 그래도 단계를 둔 이유는
    /// 이 자리가 <b>나중에 실제로 로딩할 것이 생기는 자리</b>이기 때문이다(캐릭터 정보, 인벤토리,
    /// 리소스). 그때 단계를 새로 끼워 넣으려면 Update/Draw 분기를 다시 째야 한다.
    /// </para>
    /// </summary>
    Loading,

    /// <summary>존에 들어왔다. 존 뷰/채팅/우편/쿠폰 UI가 이때만 보인다.</summary>
    InGame,
}
