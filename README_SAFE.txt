# ktin_safe_full

이 버전은 첨부 원본 ktin 소스를 기준으로, 장시간 실행 시 화면 멈춤/백화와 관련될 가능성이 큰 기능만 제거하거나 비활성화한 안전판입니다.

유지한 기능:
- 파일: 빠른 연결, 주소록, 연결 끊기, 스크립트 읽기, 메모장, 종료
- 보기: 메뉴 숨기기, ANSI 테마 선택, 화면 여백 없애기, 특수기호(F4)
- 편집: 찾기, 변수, 줄임말, 단축키 설정, 타이머, 상태바, 숫자 키패드 매크로
- 편집: 지난 화면/현재 화면을 클립보드로 복사, 파일로 저장
- 옵션: 환경 설정, 단축버튼 표시/숨김, 접속 유지
- 자동 로그인: 아이디 패턴/비번 패턴 등

제거/비활성화한 기능:
- 트리거 설정 / 실시간 하이라이트 / 트리거 액션
- 채팅 캡처 패턴 설정 / 채팅 캡처창 / 채팅 캡처 시간 표시
- 자동 갈무리(계속 파일에 쌓는 방식)

렌더링 안정성 수정:
- WM_PAINT에서 셀마다 CreateSolidBrush/DeleteObject를 반복하지 않고 ExtTextOutW(ETO_OPAQUE)로 배경을 칠하도록 수정했습니다.
- 화면 렌더링 중 정규식 매칭을 하지 않도록 차단했습니다.

빌드 예시:
cl /utf-8 /std:c++17 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX *.cpp Resource.rc user32.lib gdi32.lib shell32.lib comdlg32.lib comctl32.lib uxtheme.lib winmm.lib dwmapi.lib version.lib


[2026-07-07 build fix]
- /DNOMINMAX 사용 시 bare min/max가 안 보이던 문제를 constants.h에서 using std::min/max로 보정했습니다.
- settings.cpp의 제거된 채팅 캡처 줄 수 입력칸(hEditChatLines) 참조를 제거했습니다.
- build_msvc.bat가 Resource.rc를 cl에 직접 넘기지 않고 rc.exe로 Resource.res를 만든 뒤 링크하도록 수정했습니다.
- 압축에는 폰트 파일을 포함하지 않았습니다. Resource.rc의 MudDunggeunmo.ttf RCDATA 줄은 빌드 오류 방지를 위해 비활성화했습니다.


[buildfix14]
- 잡담 필터 기본값에 "잡담 :", "잡담:"을 추가했습니다.
- 예: "울보천사의 잡담 : 테스트"를 잡담 보기에서 잡도록 보정했습니다.
