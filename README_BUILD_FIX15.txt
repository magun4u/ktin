KTin buildfix15

수정 내용:
- 접속 유지 명령이 동작하지 않던 문제 수정.
- 원인: SendKeepAliveNow()가 g_app->isConnected == false 이면 바로 return 했음.
  안전판에서는 실제 접속되어 있어도 isConnected가 false로 남을 수 있으므로 접속 유지 명령이 차단됨.
- 변경: keepAliveEnabled가 켜져 있고 명령 문자열이 비어 있지 않으면 TinTin++ 프로세스로 그대로 전송.
