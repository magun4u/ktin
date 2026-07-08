# 변경 기록

## tail 종료/경고 정리

- `log_tail.cpp`
- tail 창 파괴 시 UI thread에서 worker를 직접 `join()`하지 않고 background retire thread에서 정리하도록 변경
- `WM_DESTROY` 경로의 pending tail UI work를 폐기해 닫기 중 RichEdit append 지연을 줄임

- `terminal_view.cpp`
- `InvalidateTerminalRows()` helper를 dirty row invalidate 경로에서 실제 사용하도록 정리해 미사용 함수 경고를 제거

- `app_version.h`, `utils.cpp`
- 버전을 `2.6.99`로 갱신
- `2.6.99` 이후 `2.6-a01` 형식으로 표시할 수 있는 alpha version macro 경로 추가

## UI 병목 세부 안정화

- `terminal_buffer.cpp`
- paint snapshot 복사를 cell 단위 조회에서 row 단위 복사로 변경해 TerminalBuffer lock 점유와 per-cell 분기 비용을 줄임
- row text snapshot도 row 포인터 기반으로 정리해 history 검색/찾기 경로의 lock 점유를 줄임

- `terminal_view.cpp`
- dirty row invalidate 계산에서 반복 metrics/cell/offset 조회를 줄임
- live scroll + dirty row 처리 시 같은 레이아웃 값을 재사용하도록 정리

- `log_tail.cpp`
- tail worker 결과 큐를 mutex 안에서 swap만 하고 UI 처리는 lock 밖에서 수행
- RichEdit append format 캐시를 추가해 동일 스타일 반복 `EM_SETCHARFORMAT` 호출을 줄임

## tail 종료 blocking 완화

- `log_tail.cpp`
- tail worker 실행 중 창 닫기 시 UI thread에서 즉시 `join()`하지 않고 숨김/비활성 후 완료 메시지에서 닫도록 조정
- 닫기 대기 중 pending UI append 작업을 버려 종료 시 RichEdit 처리 지연을 줄임
- tail worker 읽기 chunk를 줄여 닫기/종료 시 최대 대기 시간을 낮춤

## 즉시 repaint 경로 축소

- `main.cpp`, `settings.cpp`, `theme.cpp`, `terminal_view.cpp`, `shortcut_bar.cpp`, `status_bar.cpp`
- 메뉴/테마/설정/상태바 갱신 경로의 `InvalidateRect` 직후 `UpdateWindow` 강제 repaint 호출을 제거
- 즉시 erase repaint를 줄여 출력 flood 중 UI thread 재진입과 흰색 깜빡임 가능성을 낮춤

## 갈무리 tail UI append 경량화

- `log_tail.cpp`
- RichEdit append를 줄 단위 즉시 호출에서 batch append 경로로 변경
- tail worker 읽기 chunk와 초기 읽기 범위를 제한해 오래 밀린 로그가 UI thread를 장시간 점유하지 않도록 조정
- RichEdit 표시 줄 trim을 append 라인마다 수행하지 않고 batch 끝에서 1회 수행하도록 정리

## 선택 영역 repaint 축소

- `terminal_buffer.cpp`, `terminal_buffer.h`
- 선택 영역 변경 시 전체 dirty 대신 이전/신규 선택 영역의 visible row만 dirty 처리하도록 조정

- `terminal_view.cpp`
- 선택 드래그/복사/스크롤 후 전체 invalidate 호출을 dirty row invalidate 경로로 변경

## UI 페인트 경량화 추가 작업

- `terminal_view.cpp`
- 1셀 문자 출력은 foreground run 단위 `ExtTextOutW`로 묶어 GDI 호출 수를 줄임
- 2셀 문자 폭 측정 캐시를 추가해 반복 paint 비용을 낮춤

## UI 안정성 추가 작업

- `terminal_buffer.cpp`, `terminal_buffer.h`
- live scroll 발생 시 전체 dirty 대신 pending scroll row와 하단 dirty row만 기록하도록 조정

- `terminal_view.cpp`
- dirty 처리에서 live scroll row를 `ScrollWindowEx`로 이동시키고 필요한 row만 invalidate하도록 조정


## 빌드와 소스 정리

- `Makefile`
- WSL MinGW 빌드 규칙을 추가.
- 리소스 파일 처리에 UTF-8 코드페이지 지정.
- MinGW 실행 파일에 런타임 정적 링크 적용.
- 리소스 제외 빌드 경로인 `make nores` 추가.
- `make clean`에서 `*.o`, `*.obj`, `*.res`, `*.aps`, `ktin.exe`를 삭제하도록 정리

- `Resource.rc`
- UTF-16LE 파일을 UTF-8로 변환해 `windres`의 널 문자 경고와 전처리 오류 수정

- `.gitignore`
- 빌드 산출물, 로컬 설정 파일, 실행 파일, 로그, `bin/` 디렉토리를 제외하도록 추가

- 저장소 정리
- 빌드 산출물과 개인 설정 파일을 소스 패키지에서 제외

## WSL / MinGW 경고 정리

- `dialogs.cpp`, `dialogs.h`
- 내부 함수 선언 충돌과 WinAPI 구조체 초기화 경고 정리

- `settings.cpp`, `settings.h`
- 설정 미리보기 선언 오류와 미사용/들여쓰기 경고 정리

- `main.cpp`, `main.h`
- MinGW 선언 충돌, MSVC 전용 지시문, WinAPI 초기화 경고 정리

- `memo.cpp`, `memo.h`
- 파일 경로 처리 오류, 선언 충돌, 미사용 함수와 미사용 인자 경고 정리

- `highlight.cpp`, `highlight.h`
- 선언 충돌과 구조체 초기화 경고 정리

- `input.cpp`, `input.h`
- 내부 선언 충돌과 미사용 변수 경고 정리

- `shortcut_bar.h`, `status_bar.cpp`, `status_bar.h`
- 프로시저 선언 충돌과 미사용 인자 경고 정리

- `chat_capture.h`
- 플로팅 창 프로시저 선언 충돌 정리

- `address_book.cpp`, `auto_login.cpp`, `help_dialog.cpp`, `log_tail.cpp`, `terminal_buffer.cpp`, `timer.cpp`
- 미사용 변수/함수, 들여쓰기, 조건식 경고 정리

- `utils.cpp`, `utils.h`
- 선언 충돌, 미사용 인자, 초기화 경고 정리

## KTin 정리 작업

- `Makefile`
- 정리 대상을 목적 파일, 실행 파일, MSVC 임시 파일 계열로 분리

- `.gitignore`
- MinGW/MSVC 빌드 산출물, 로컬 실행 로그, 로컬 설정 파일 제외 규칙 확장

- `main.cpp`
- 입력창, 상태바, 하이라이트 대화상자, 채팅 플로팅 창의 중복 구현 제거, 각 전용 파일로 소유권 변경

- `async_file_writer.cpp`, `async_file_writer.h`
- 갈무리와 채팅 로그 쓰기를 위한 비동기 파일 작성기 추가

- `main.cpp`, `main.h`
- 갈무리 로그 핸들 소유 구조를 정리하고 활성 상태 플래그와 비동기 작성기 소유 구조로 전환
- 공개 헤더의 중복 선언을 줄이고, 각 기능의 선언 위치를 전용 헤더로 정리

- `settings.cpp`, `highlight.cpp`
- 미리보기/색상 박스 브러시를 재사용하도록 바꿔 `WM_CTLCOLORSTATIC` 처리 중 GDI 브러시 누수 가능성 줄임

- 콘솔 시작 처리
- ConPTY 시작 실패 시 파이프, 속성 목록, 의사 콘솔 핸들 정리를 보강

## KTin 수정 03

- `chat_capture.cpp`, `chat_capture.h`
- 채팅 로그 쓰기를 일자별 비동기 작성기 재사용 방식으로 변경
- 한 줄을 쓸 때마다 작성기와 스레드를 새로 만들던 구조 제거
- 종료 시 채팅 로그 작성기를 명시적으로 닫도록 수정

- `async_file_writer.cpp`
- 부분 쓰기와 큰 데이터 조각 쓰기를 더 안전하게 처리하도록 반복 쓰기 루프 보강

- `main.cpp`
- 최종 종료 과정에서 채팅 로그 작성기를 닫도록  수정
- UI 프로시저 분리 후 남은 빈 구역 표시 주석 제거

- `main.h`
- `chat_capture.h`, `dialogs.h`가 소유해야 할 중복 선언 제거

## KTin 수정 04

- `Makefile`
- WSL 설치 대상 경로로 `INSTALL_DIR ?= /mnt/c/ktin`을 추가
- `make install` 실행 시 `ktin.exe`를 빌드하고 `/mnt/c/ktin/ktin.exe`로 복사하게 수정. 

## KTin 수정 05

- `Makefile`
- `make install`을 실제 빌드 대상인 `all`에 의존하는 명시적 대상로 정리
- `install-wsl` 별칭을 추가
- 설치 대상은 `/mnt/c/ktin/ktin.exe`로 유지

- `Resource.rc`
- 버전 리소스가 중앙 버전 매크로를 읽도록 변경

- `utils.cpp`
- 실행 중 표시 버전이 `주.부.패치.번호` 형식으로 나오도록 변경
- 예비 버전 문자열도 중앙 버전 값을 사용하도록 

- `app_version.h`
- 버전 증가를 한 곳에서 관리하기 위한 중앙 버전 정의 파일 추가

## KTin 수정 06

- `app_version.h`
- 버전 문자열을 별도 수동 문자열이 아니라 숫자 버전 매크로에서 조립하도록 변경
- 이후 버전 변경 시 이 파일의 숫자 필드만 수정하면 되도록 정리

- `Resource.rc`
- 파일/제품 버전이 계속 `app_version.h`의 버전 매크로를 읽도록 유지

- `utils.cpp`, `utils.h`
- 실행 버전 예비 문자열을 `KTIN_APP_VER_*` 값에서 조립하도록 정리
- 팝업/바로가기 UI 글꼴 캐시 정리 함수를 추가

- `main.cpp`
- 종료 시 UI 글꼴 캐시 정리 

- `Makefile`
- `app_version.h` 변경 시 리소스와 버전 표시 코드가 다시 빌드되도록 의존성을 추가
- `make install`과 `install-wsl`의 복사 경로를 `/mnt/c/ktin/ktin.exe`로 유지

## KTin 수정 07

- `app_version.h`
- KTin GUI 버전과 TinTin++ 기준 버전을 별도 중앙 필드로 분리

- `Resource.rc`
- 파일/제품 버전 문자열이 중앙 KTin GUI 버전을 사용

- `utils.cpp`, `utils.h`
- `GetAppVersionString()`이 KTin GUI 버전을 반환하도록 변경
- TinTin++ 기준 버전 조회 함수 분리

- `main.cpp`
- 메인 창 제목에 KTin GUI 버전이 함께 표시되도록 변경 

## KTin 수정 08

- `app_version.h`
- 두 자리 빌드 문자열을 별도 수동 문자열로 관리하지 않도록 숫자 빌드값에서 자동 선택하는 매크로를 추가
- 버전 변경 시 KTin GUI 숫자 버전만 올리면 리소스와 실행 표시 문자열이 함께 반영되도록 정리

- `utils.cpp`
- 트레이 아이콘 도움말에도 `TinTin++ GUI v버전` 형식으로 현재 KTin GUI 버전 표시 

## KTin 수정 09

- `main.cpp`
- 터미널 글자 렌더링 중 매 문자마다 같은 글꼴을 다시 선택하던 중복 처리 제거
- 1칸 문자는 글리프 폭 측정을 건너뛰고, 2칸 문자에서만 폭을 측정하도록 변경

## KTin 수정 10

- `Makefile`
- 자동 의존성 파일 생성을 추가
- 헤더 변경 시 관련 목적 파일이 다시 빌드되도록 정리
- `make clean`에서 의존성 파일도 삭제하도록 수정

- `utils.cpp`, `utils.h`, `main.cpp`
- 클립보드 복사 처리를 공용 함수로 정리
- 클립보드 메모리 잠금 실패 시 메모리 해제를 보강
- 파일 저장 시 부분 쓰기 방지를 위한 반복 쓰기 처리 적용

- `async_file_writer.cpp`
- BOM 기록과 비동기 쓰기 모두 공용 반복 쓰기 함수를 사용하도록 정리

## KTin 수정 11

- `process_manager.cpp`, `process_manager.h`
- TinTin++ 프로세스 시작과 종료 처리를 `main.cpp`에서 분리
- ConPTY 파이프와 프로세스 핸들 정리 경로를 전용 함수로 정리
- 사용하지 않는 프로세스 기본 스레드 핸들을 시작 직후 닫도록 변경

- `main.cpp`, `main.h`, `terminal_buffer.h`
- 프로세스 시작/종료 선언을 전용 헤더로 이동
- ConPTY 공개 선언을 터미널 버퍼 헤더로 이동
- `main.cpp`의 프로세스 관리 책임 축소

## KTin 수정 12

- `Makefile`
- 패키지 버전이 바뀔 때 기존 목적 파일과 의존성 파일을 자동 정리하도록 빌드 스탬프 추가
- 이전 빌드의 오래된 `main.o`가 남아 `process_manager.o`와 중복 링크되는 문제 방지

- `.gitignore`
- 자동 의존성 파일과 빌드 스탬프 파일 제외 규칙 추가

- `terminal_view.cpp`, `terminal_view.h`
- 터미널 창 프로시저와 로그창 드래그 선택 처리를 `main.cpp`에서 분리

- `main.cpp`, `main.h`
- 터미널 창 처리 선언을 전용 헤더로 이동
- `main.cpp`의 터미널 렌더링 책임 축소

## KTin 수정 13

- `main_commands.cpp`, `main_commands.h`
- 메인 메뉴 명령 처리를 `main.cpp`에서 분리
- 새 KTin 창 실행 처리를 메뉴 명령 전용 파일로 이동
- 메뉴 처리 후 입력창 포커스 복구 처리를 공용 함수로 정리

- `main.cpp`
- `WM_COMMAND` 처리 책임을 전용 명령 처리 함수로 축소
- 메뉴 명령 처리 분리로 메인 윈도우 프로시저 크기 감소

## KTin 수정 14

- `input.cpp`, `input.h`
- 입력창 편집 서브클래스 프로시저를 `main.cpp`에서 입력 전용 파일로 이동

- `chat_capture.cpp`, `chat_capture.h`
- 채팅 입력창 서브클래스 프로시저를 채팅 캡처 전용 파일로 이동

- `shortcut_bar.cpp`, `shortcut_bar.h`
- 단축 버튼 바 창 프로시저를 단축 버튼 전용 파일로 이동

- `main.cpp`, `main.h`
- UI 서브클래스/단축 버튼 처리 선언을 전용 헤더 소유로 정리
- 메인 파일의 창 프로시저 책임을 축소

## KTin 수정 15

- `log_chunk.h`
- 터미널 출력 갱신 메시지에 사용하는 로그 조각 구조체를 별도 헤더로 분리

- `process_manager.cpp`, `process_manager.h`
- TinTin++ 출력 읽기 스레드 처리를 프로세스 관리 파일로 이동
- ANSI 파싱, 화면 갱신 메시지 전송, 원본 로그 버퍼 갱신 책임을 `main.cpp`에서 분리

- `main.cpp`, `main.h`
- 읽기 스레드 선언과 구현을 전용 파일 소유로 정리
- 메인 파일의 프로세스/출력 처리 책임 축소

## KTin 수정 16

- `global_shortcuts.cpp`, `global_shortcuts.h`
- 전역 메뉴 단축키 처리를 `main.cpp`에서 분리
- 입력창에서도 같은 단축키 처리 함수를 명시적으로 참조하도록 정리

- `main.cpp`, `main.h`, `input.cpp`
- 전역 메뉴 단축키의 중복/비공개 선언 문제 정리
- 메인 창 배경 브러시를 매번 생성하지 않고 색상 변경 시에만 재생성하도록 변경
- 종료 시 메인 배경 브러시를 명시적으로 해제하도록 수정

## KTin 수정 17

- `ui_resources.cpp`, `ui_resources.h`
- 메인/입력창 GDI 브러시와 글꼴 정리 함수를 분리
- 종료 시 로그/입력/채팅 글꼴과 입력/메인 배경 브러시를 한 경로에서 정리하도록 변경

- `main.cpp`, `input.cpp`
- 배경 그리기 중 반복 생성하던 브러시 사용을 캐시 재사용 방식으로 변경
- 종료 루틴의 GDI 객체 해제 코드를 공용 정리 함수 호출로 축소

## KTin 수정 18

- `terminal_view.cpp`
- 터미널 그리기용 메모리 DC/비트맵 생성 실패 경로를 보강
- 백버퍼 생성 실패 시 직접 그리기로 폴백하도록 수정
- GDI 객체 선택/해제 경로에서 널 핸들 사용 가능성을 줄임

- `main.cpp`
- RichEdit 라이브러리 로딩을 공용 함수 경로로 통일
- 종료 시 로딩된 RichEdit 라이브러리를 명시적으로 해제하도록 수정

## KTin 수정 19

- `win_handle.h`
- Win32 파일 핸들을 자동으로 닫는 작은 핸들 소유 래퍼 추가

- `log_tail.cpp`
- 갈무리 tail 파일 크기 확인과 범위 읽기 경로를 자동 핸들 정리 방식으로 변경
- 파일 위치 이동 실패 시 잘못된 위치에서 읽지 않도록 방어 처리 추가

- `utils.cpp`, `async_file_writer.cpp`
- 파일 저장과 비동기 파일 열기 실패 경로에서 핸들이 누락되지 않도록 정리
- 파일 저장 열기 실패 시 사용자에게 오류를 표시하도록 수정

## KTin 수정 20

- `win_gdi.h`
- GDI 객체 자동 해제와 선택 복구를 위한 작은 RAII 헬퍼 추가

- `about_dialog.cpp`, `status_bar.cpp`, `shortcut_bar.cpp`, `utils.cpp`
- 반복 그리기 경로에서 생성하는 브러시와 펜을 자동 해제 방식으로 변경
- GDI 객체 선택 후 원래 객체 복구가 누락될 가능성을 줄이도록 정리

## KTin 수정 21

- `file_io.cpp`, `file_io.h`
- Win32 파일 쓰기 공용 함수를 추가
- 부분 쓰기가 발생해도 전체 데이터가 기록되도록 반복 쓰기 경로를 공용화

- `async_file_writer.cpp`, `utils.cpp`, `memo.cpp`
- 비동기 로그 쓰기, 텍스트 저장, 메모장 자동저장/저장 경로가 공용 파일 쓰기 함수를 사용하도록 변경
- 메모장 저장 실패 시 성공 처리로 넘어가지 않도록 방어 처리 추가

## KTin 수정 22

- `win_dc.h`
- 윈도우 DC를 자동으로 해제하는 범위 기반 헬퍼 추가

- `input.cpp`, `memo.cpp`, `utils.cpp`
- `GetDC` / `ReleaseDC` 수동 해제 경로를 범위 기반 해제 구조로 변경
- 폰트 측정과 메뉴 측정 중 조기 반환이 발생해도 DC가 해제되도록 정리

- `utils.cpp`
- TinTin++ 명령 전송 시 `WriteFile` 단발 호출 대신 전체 데이터 쓰기 공용 함수를 사용하도록 변경

## KTin 수정 23

- `win_paint.h`
- `BeginPaint` / `EndPaint`를 자동으로 짝지어 처리하는 범위 기반 헬퍼 추가

- `main.cpp`, `input.cpp`, `memo.cpp`, `shortcut_bar.cpp`, `status_bar.cpp`, `terminal_view.cpp`, `theme.cpp`
- 페인트 처리 중 조기 반환이 발생해도 `EndPaint`가 호출되도록 정리
- 수동 페인트 종료 코드를 공용 범위 기반 처리로 변경

## KTin 수정 24

- `theme.cpp`
- ANSI 테마 미리보기와 테마 목록 그리기에서 생성하는 브러시를 자동 해제 방식으로 변경
- 테마 미리보기 폰트 선택을 범위 기반 복구 방식으로 변경
- 테마 적용 중 새 글꼴 생성 실패 시 이미 생성된 글꼴이 자동 해제되도록 정리
- 기존 로그/입력/채팅 글꼴 교체 후 해제 경로를 자동 해제 방식으로 변경

## KTin 수정 25

- `win_menu.h`
- 팝업 메뉴 핸들을 자동으로 해제하는 범위 기반 헬퍼 추가

- `main.cpp`, `chat_capture.cpp`, `log_tail.cpp`, `terminal_view.cpp`
- 우클릭 팝업 메뉴 생성/해제 경로를 자동 해제 방식으로 변경
- 팝업 메뉴 생성 실패 시 조기 반환하는 방어 처리 추가
- 메뉴 추적 이후 수동 `DestroyMenu` 호출을 제거하고 소유권 정리 경로를 단일화

## KTin 수정 26

- `win_find.h`
- 파일 검색 핸들을 자동으로 닫는 범위 기반 헬퍼 추가

- `memo.cpp`
- 자동 저장 파일 검색 경로에서 `FindClose` 수동 호출을 자동 해제 방식으로 변경
- 자동 저장 파일 시간 변환 구조체 초기화를 보강

- `ui_resources.cpp`, `ui_resources.h`, `settings.cpp`
- 입력창 브러시 재생성 경로를 UI 리소스 전용 함수로 통합
- 설정 변경 중 브러시 해제와 재생성 처리를 단일 경로로 정리

- `settings.cpp`, `highlight.cpp`, `dialogs.cpp`
- 목록 그리기 중 생성하는 브러시를 자동 해제 방식으로 변경
- 글꼴 선택 복구를 범위 기반 처리로 변경
- 정보 창 종료 시 배경 브러시 포인터를 함께 초기화하도록 정리

## KTin 수정 27


- `win_gdi.h`
- 단색 영역 채우기 공용 함수를 추가
- GDI 선택 복구 헬퍼의 선택 성공 여부 확인 함수를 추가

- `timer.cpp`, `terminal_view.cpp`
- 반복 그리기 중 직접 생성하던 브러시 사용을 공용 함수로 변경
- 글꼴 선택 복구를 범위 기반 처리로 변경

- `file_io.cpp`
- 이어쓰기 위치 이동 실패 시 쓰기를 중단하도록 방어 처리 추가

## KTin 수정 28

- `terminal_view.cpp`
- 백버퍼 크기 계산 중 `int`와 `LONG`이 섞여 MinGW에서 컴파일 오류가 나던 부분 수정

- `win_rect.h`
- `RECT` 너비와 높이를 `int`로 안전하게 계산하는 공용 헬퍼 추가

- `main.cpp`, `input.cpp`, `status_bar.cpp`, `terminal_view.cpp`
- 창 크기 계산 경로 일부를 공용 `RECT` 헬퍼 사용 방식으로 정리


## KTin 수정 29

- `ui_resources.cpp`
- `DeleteBrush` 매크로 이름 충돌로 MinGW 컴파일이 실패하던 부분 수정
- UI 브러시 해제 함수 이름을 WinGDI 매크로와 충돌하지 않도록 변경

- `utils.cpp`
- 메뉴 측정과 메뉴 그리기 중 선택한 글꼴을 범위 기반으로 복구하도록 정리
- 메뉴 측정 결과 계산에 공용 `RECT` 헬퍼를 사용하도록 변경

- `dialogs.cpp`, `help_dialog.cpp`, `log_tail.cpp`, `memo.cpp`, `settings.cpp`
- 창 크기와 저장 크기 계산 경로 일부를 공용 `RECT` 헬퍼 사용 방식으로 정리

## KTin 수정 30

- `main_events.cpp`, `main_events.h`
- 메인 창 타이머 처리, TinTin++ 변수 갱신 처리, 트레이 아이콘 처리를 전용 파일로 분리
- 세션 활성 상태와 `uptime` 변수 갱신 경로를 공용 함수로 정리
- 트레이 아이콘 복구/종료 메뉴 처리 경로를 단일화

- `constants.h`, `theme.cpp`, `main.cpp`
- `WM_APP + 4` 직접 사용을 `WM_APP_VAR_UPDATE` 상수로 변경
- 메인 창 페인트 처리 중 같은 배경을 두 번 칠하던 중복 처리 제거

- `address_book.h`, `chat_capture.h`
- 필요한 자료형 헤더를 직접 포함하도록 정리

## KTin 수정 31

- `win_menu.h`
- 창 메뉴 교체 시 기존 메뉴를 안전하게 해제하는 공용 함수를 추가

- `log_tail.cpp`
- 갈무리 보기 창의 메뉴 숨김/표시, 상태바 전환, 항상 위 전환 중 기존 메뉴가 남을 수 있던 경로 정리
- 메뉴 생성 실패 시 새 메뉴를 해제하고 기존 메뉴 상태를 보존하도록 처리
- 메뉴 교체 후 `DrawMenuBar` 호출 경로를 공용 함수로 통합

## KTin 수정 32

- `main_layout.cpp`, `main_layout.h`
- 메인 창 자식 컨트롤 배치 처리를 전용 파일로 분리
- `BeginDeferWindowPos` 실패 이후 잘못된 배치 호출이 이어지지 않도록 방어 처리 추가

- `main.cpp`, `main.h`
- 레이아웃 처리 책임을 줄이고 공개 선언을 전용 헤더로 이동
- 메인 창 페인트 처리의 중복 클라이언트 영역 조회 제거

- `main_commands.cpp`, `shortcut_bar.cpp`, `terminal_view.cpp`, `theme.cpp`
- 레이아웃 갱신 선언을 전용 헤더 참조 방식으로 정리

## KTin 수정 33

- `win_dc.h`
- 호환 메모리 DC를 자동 해제하는 공용 헬퍼 추가

- `terminal_view.cpp`
- 터미널 백버퍼용 메모리 DC 해제를 자동 정리 방식으로 변경
- 백버퍼 비트맵 해제를 기존 GDI 객체 정리 헬퍼로 통합
- 백버퍼 복사 크기 계산에 공용 `RECT` 헬퍼 사용

## KTin 수정 34

- `main_window.cpp`, `main_window.h`
- 메인 창 클래스 등록과 메인 창 생성을 전용 파일로 분리
- 창 클래스가 이미 등록된 경우를 정상 상태로 처리하도록 방어 처리 추가

- `main.cpp`
- 시작 루틴에서 창 클래스 등록 세부 구현을 제거하고 전용 함수 호출 방식으로 정리

## KTin 수정 35

- `main_events.cpp`, `main_events.h`
- 메인 창 크기 변경, 이동, 크기 조절 종료 처리를 전용 함수로 분리
- 마지막 메인 창 위치 추적 상태를 메인 이벤트 처리 파일로 이동
- 창 위치 저장과 갈무리 보기 창 위치 동기화 경로를 공용 처리로 정리

- `main.cpp`
- 크기 변경/이동 처리 책임을 줄이고 메인 윈도우 프로시저의 분기만 남기도록 정리

## KTin 수정 36

- `main_startup.cpp`, `main_startup.h`
- 메인 창 생성 초기화 처리를 전용 파일로 분리
- 터미널 버퍼, 메인 UI 컨트롤, 입력창 생성 실패 시 즉시 중단하도록 방어 처리 추가
- 시작 중 오류 메시지와 종료 요청 경로를 공용 함수로 정리

- `main.cpp`
- `WM_CREATE` 처리 책임을 줄이고 초기화 함수 호출만 남기도록 정리


## KTin 수정 37

- `main_events.cpp`, `main_events.h`
- 메인 창 페인트, 포커스, 닫기, 종료 처리를 전용 함수로 분리
- 종료 시 저장, 로그 종료, 프로세스 종료, GDI/RichEdit 정리 경로를 한 곳으로 정리
- 프로세스 종료 알림 처리와 입력창 비활성화 경로를 전용 함수로 이동

- `main.cpp`
- 메인 윈도우 프로시저의 직접 처리 분기를 줄이고 이벤트 처리 함수 호출 방식으로 정리

## KTin 수정 38

- `app_state.h`
- 메인 상태 구조체와 검색 상태 구조체를 전용 헤더로 분리
- `main.h`에 몰려 있던 상태 정의를 줄여 공개 헤더 책임 축소

- `main_message_loop.cpp`, `main_message_loop.h`
- 모달리스 검색/메모 검색/특수기호 창 메시지 처리를 전용 파일로 분리
- 메인 메시지 루프에서 대화상자 메시지 분기 중복을 제거

- `main.cpp`, `main.h`
- 시작 루틴과 메인 헤더의 직접 책임을 줄이고 전용 파일 참조 방식으로 정리

## KTin 수정 39

- `main_events.cpp`, `main_events.h`
- 메인 메뉴 초기화, 커스텀 메뉴 클릭, 메뉴 숨김 상태의 우클릭 처리 경로를 전용 함수로 분리
- 메뉴 상태 갱신과 메뉴 표시 복구 처리를 메인 이벤트 처리 파일로 이동

- `main_window.cpp`, `main_window.h`
- 메인/터미널/입력/단축바/상태바 창 클래스 이름 소유권을 메인 창 전용 파일로 이동

- `main_startup.cpp`, `main.cpp`, `main.h`
- 창 클래스 이름 참조 위치를 전용 헤더 기준으로 정리
- 메인 윈도우 프로시저와 공개 헤더의 직접 책임을 추가 축소

## KTin 수정 40

- `main_startup.cpp`
- 메인 창 초기화 실패 시 생성된 자식 창, 터미널 버퍼, GDI 자원을 정리하도록 보강
- TinTin++ 출력 읽기 스레드 시작 실패 시 프로세스와 핸들을 정리하고 초기화를 중단하도록 수정

- `main.cpp`
- 시작 과정에서 내장 글꼴 등록을 중복 호출하던 경로 제거

## KTin 수정 41

- `constants.h`, `main_events.cpp`
- 세션 경과 시간 타이머와 창 설정 지연 저장 타이머의 ID 충돌을 분리
- 중복 `switch case`가 생길 수 있던 타이머 처리 경로 정리
- 종료 시 자동 재접속, 전환 접속, 로그 다시그리기, 사용자 타이머까지 함께 중지하도록 보강


## KTin 수정 42

- `main_events.cpp`, `main_events.h`
- 터미널 출력 조각 수신 처리를 메인 이벤트 처리 파일로 분리
- 로그 다시그리기 예약 경로를 전용 함수로 정리
- 세션 경과 시간 갱신 함수의 미사용 인자 제거

- `main.cpp`
- `WM_APP_LOG_CHUNK` 직접 처리 분기를 줄이고 이벤트 처리 함수 호출 방식으로 정리
- 사용하지 않는 헤더 포함 일부 정리

## KTin 수정 43

- `session_state.cpp`, `session_state.h`
- 세션 활성 상태, 세션 경과 시간, 세션 변수 갱신 처리를 전용 파일로 분리
- `uptime` 변수 갱신과 상태바 다시그리기 경로를 한 곳으로 정리

- `main_events.cpp`
- 세션 상태/변수 직접 처리 코드를 제거하고 전용 함수 호출 방식으로 정리
- 메인 이벤트 처리 파일의 변수 관리 책임 축소

## KTin 수정 44

- `main_timers.cpp`, `main_timers.h`
- 메인 창 타이머 처리와 전체 타이머 중지 경로를 전용 파일로 분리
- 로그 다시그리기 예약 처리를 전용 함수로 정리

- `main_events.cpp`, `main_events.h`, `main.cpp`
- 타이머 처리 직접 책임을 줄이고 이벤트 처리 파일의 역할을 축소
- 종료 시 타이머 정리 호출은 전용 타이머 파일 기준으로 유지

## KTin 수정 45

- `main_shutdown.cpp`, `main_shutdown.h`
- 메인 창 종료 처리와 종료 시 자원 저장/해제 경로를 전용 파일로 분리
- 로그 종료, 프로세스 종료, 타이머 정리, GDI/RichEdit 해제 순서를 한 곳에서 유지하도록 정리

- `main_events.cpp`
- `WM_DESTROY` 직접 처리 책임을 줄이고 종료 전용 함수 호출 방식으로 변경

## KTin 수정 46

- `main_runtime.cpp`, `main_runtime.h`
- 공용 컨트롤 초기화와 RichEdit 로딩을 메인 런타임 초기화 함수로 분리
- 메인 메시지 루프를 전용 함수로 분리
- `GetMessageW` 실패 시 비정상 종료 코드를 반환하도록 방어 처리 추가

- `main.cpp`
- 프로그램 진입점의 초기화와 메시지 루프 직접 처리 책임 축소

## KTin 수정 47

- `main_window.cpp`, `main_window.h`
- 메인 윈도우 프로시저를 메인 창 전용 파일로 이동
- 창 클래스 등록과 메시지 분기 처리 소유권을 같은 파일 기준으로 정리

- `find_state.cpp`
- 검색 상태 전역 변수 정의를 진입점 파일에서 분리

- `main.cpp`, `main.h`
- 프로그램 진입점 파일에 남아 있던 메인 윈도우 프로시저와 검색 상태 정의를 제거
- 공개 헤더의 메인 윈도우 프로시저 선언 위치를 메인 창 전용 헤더로 이동

## KTin 수정 48

- `main_runtime.cpp`, `main_runtime.h`
- RichEdit 라이브러리 핸들 소유권을 메인 런타임 파일로 이동
- 시작 실패와 정상 종료 양쪽에서 같은 정리 함수를 사용하도록 정리

- `main.cpp`, `main_shutdown.cpp`, `main.h`
- 초기화 실패 시 런타임 자원을 정리하도록 보강
- 종료 처리에서 RichEdit 해제를 런타임 정리 함수 호출로 변경
- 공개 헤더의 런타임 핸들 직접 노출 제거

## KTin 수정 49

- `main_runtime.cpp`, `main_runtime.h`
- RichEdit 라이브러리 로딩 함수를 메인 런타임 파일 소유로 이동
- RichEdit 로딩 성공 여부를 반환하도록 바꿔 시작 실패 방어 처리 강화
- RichEdit 라이브러리 핸들을 외부에 직접 노출하지 않도록 정리

- `utils.cpp`, `utils.h`, `log_tail.cpp`, `memo.cpp`
- RichEdit 로딩 함수 구현과 선언 위치를 정리
- 로그 보기와 메모 창에서 런타임 헤더를 통해 RichEdit 로딩 함수를 사용하도록 변경

## KTin 수정 50

- `status_bar.cpp`
- 메인 메뉴 생성 실패 시 중간에 생성된 메뉴 핸들이 남지 않도록 자동 해제 구조로 변경
- 메인 메뉴 재생성 시 기존 메뉴 핸들을 먼저 정리하도록 수정

- `memo.cpp`
- 메모장 메뉴 생성 경로를 자동 해제 구조로 변경
- 메뉴 교체 시 기존 메뉴 해제와 메뉴바 다시그리기를 공용 처리로 변경

## KTin 수정 51

- `win_gdi.h`
- GDI 핸들 참조를 안전하게 해제하고 널 처리하는 공용 함수를 추가

- `about_dialog.cpp`, `dialogs.cpp`, `help_dialog.cpp`, `highlight.cpp`, `settings.cpp`, `utils.cpp`, `memo.cpp`, `ui_resources.cpp`
- 폰트와 브러시 수동 해제 코드를 공용 함수 사용 방식으로 정리
- 반복되는 `DeleteObject` 직접 호출을 줄여 해제 누락과 매크로 충돌 가능성을 낮춤

## KTin 수정 52

- `main_layout.cpp`
- 상태바 높이 함수 선언을 찾지 못해 발생하던 빌드 오류를 수정
- 레이아웃 계산에서 필요한 유틸리티 헤더 포함을 보강

- `main_events.cpp`
- 포커스 처리와 프로세스 종료 처리 함수의 미사용 인자 경고를 정리


## KTin 수정 53

- `ui_resources.cpp`
- 메인/입력창 브러시 재생성 중 새 브러시 생성 실패 시 기존 브러시를 유지하도록 수정
- 브러시 교체 순서를 생성 성공 후 기존 자원 해제 방식으로 변경해 실패 경로의 화면 자원 손실 가능성을 줄임


## KTin 수정 54

- `app_state.cpp`, `app_state.h`
- 터미널 버퍼 소유권을 수동 포인터에서 자동 정리 구조로 변경
- 전역 상태 객체가 종료될 때 터미널 버퍼가 자동으로 정리되도록 생성자와 소멸자 정의를 분리

- `main_startup.cpp`, `main_shutdown.cpp`
- 시작 실패와 정상 종료 경로에서 터미널 버퍼를 직접 삭제하지 않고 소유 객체의 정리 함수로 처리하도록 변경
- 터미널 버퍼 해제 경로를 단순화해 중복 해제 가능성을 줄임


## KTin 수정 55

- `main_startup.cpp`
- 시작 시 `uptime` 변수 초기화를 세션 상태 전용 함수로 통합
- 직접 변수 목록을 순회하던 중복 처리 제거

- `auto_login.cpp`, `utils.cpp`
- 접속 성공/종료/재접속 준비 시 세션 활성 상태 변경을 `session_state` 경로로 통합
- 세션 타이머 중지와 `uptime` 초기화가 누락될 수 있는 직접 상태 변경을 줄임

## KTin 수정 56

- `win_proc_attr.h`
- 프로세스 시작 확장 속성 목록을 자동 정리하는 전용 헬퍼를 추가

- `process_manager.cpp`
- ConPTY 프로세스 시작 중 속성 목록 해제 경로를 자동 정리 구조로 변경
- 프로세스 시작 실패 경로의 수동 정리 코드를 줄여 누락 가능성을 낮춤

## KTin 수정 57

- `process_manager.cpp`
- 출력 읽기 스레드에서 로그 조각 메시지 전송 실패 시 할당된 로그 조각이 남지 않도록 정리
- 로그 조각 생성 실패 시 대기 중인 출력 조각을 정리해 메모리 누적 가능성을 줄임

## KTin 수정 58

- `win_handle.h`
- `HANDLE` 참조를 안전하게 닫고 널 처리하는 공용 함수를 추가

- `process_manager.cpp`
- ConPTY 시작 과정의 파이프 핸들을 자동 정리 구조로 변경
- 프로세스 시작 실패 경로의 수동 핸들 정리 코드를 줄여 누락 가능성을 낮춤
- 프로세스 스레드 핸들 해제도 공용 함수 사용 방식으로 정리

## KTin 수정 59

- `async_file_writer.cpp`
- 비동기 파일 작성기 시작 중 스레드 생성 실패 시 열린 파일 핸들과 내부 상태를 정리하도록 보강
- 이어쓰기 위치 이동을 실패 확인 가능한 방식으로 변경해 로그 파일 열기 실패 경로를 명확하게 처리
- 작성기 종료 시 파일 핸들 해제를 공용 핸들 정리 함수 사용 방식으로 변경

## KTin 수정 60

- `win_timer.h`
- Win32 타이머 시작, 중지, 재시작을 공용 함수로 정리

- `main_timers.cpp`, `session_state.cpp`, `settings.cpp`
- 메인 타이머 중지와 로그 다시그리기 예약 처리를 공용 타이머 함수 사용 방식으로 변경
- 세션 경과 시간 타이머와 설정 지연 저장 타이머의 수동 중지/재시작 경로를 정리

## KTin 수정 61

- `status_bar.cpp`, `memo.cpp`, `utils.cpp`
- 글꼴 선택 후 수동 복구하던 일부 그리기/측정 경로를 범위 기반 복구 방식으로 변경
- 조기 반환이나 예외적인 실패 경로에서 기존 GDI 글꼴 복구가 누락될 가능성을 줄임
## KTin 수정 62

- `address_book.cpp`, `auto_login.cpp`, `dialogs.cpp`, `main_commands.cpp`
- 접속 전환과 자동 재접속 타이머 시작/중지 경로를 공용 타이머 함수 사용 방식으로 정리
- 타이머 재시작 시 기존 타이머 중지 후 다시 시작하는 흐름을 명확하게 변경

- `log_tail.cpp`, `memo.cpp`, `terminal_view.cpp`
- 갈무리 보기, 메모장 자동저장, 로그 드래그 스크롤 타이머 처리를 공용 타이머 함수로 통합
- 직접 `SetTimer`/`KillTimer`를 호출하던 남은 경로를 제거해 타이머 처리 방식의 일관성을 높임


## KTin 수정 63

- `main_startup.cpp`
- 시작 실패 정리 경로에서 GDI 자원 정리 함수 선언 누락으로 발생하던 빌드 오류 수정

- `win_menu.h`, `status_bar.cpp`, `main_shutdown.cpp`
- 메인 메뉴 핸들 해제를 공용 함수로 정리
- 종료 시 메인 메뉴 핸들이 남을 수 있는 경로를 보강

## KTin 수정 64

- `main_startup.cpp`, `main_events.cpp`, `main_window.cpp`
- TinTin++/ConPTY 백엔드 시작을 메인 창 생성 이후 메시지 처리 단계로 이동
- 백엔드 시작 실패가 메인 창 생성 실패로 이어져 더블클릭 실행 시 아무 창도 뜨지 않는 회귀를 수정
- 백엔드 시작 실패 시 GUI는 유지하고 경고만 표시하도록 변경

- `constants.h`, `main_events.h`
- 백엔드 지연 시작용 내부 메시지를 추가

## KTin 수정 64_01

- `Resource.rc`
- MinGW 빌드에서도 Common Controls v6 manifest가 실행 파일 내부에 포함되도록 보강
- `ktin.exe.manifest`가 실행 파일 옆에 없을 때 초기화가 조용히 실패할 가능성을 줄임

- `main_runtime.cpp`, `main.cpp`
- Common Controls/RichEdit 보조 초기화 실패가 메인 창 기동 실패로 이어지지 않도록 완화
- 초기화/창 클래스/메인 창 생성 실패 시 조용히 종료하지 않고 오류 메시지를 표시하도록 보강

- `settings.cpp`
- 저장된 창 좌표가 현재 화면 밖에 있을 때 메인 창을 보이는 작업 영역 안으로 보정

## KTin 구조/성능 정리

- `main.cpp`, `main.h`
- 과도하게 분리된 main/window/runtime 보조 파일을 2500줄 미만 범위에서 다시 통합
- 로그 redraw 메시지를 payload 없는 dirty 이벤트로 단순화

- `win_util.h`
- 작은 Win32 RAII/헬퍼 헤더들을 단일 유틸 헤더로 통합

- `terminal_buffer.cpp`, `terminal_buffer.h`
- `ScrollUp()`의 셀 단위 전체 복사를 행 블록 이동으로 변경
- 마지막 칸 출력 후 delayed wrap 상태를 추가해 다음 printable 문자에서 줄바꿈 처리
- history row 폭 불일치 접근을 방어

- `theme.cpp`, `theme.h`, `process_manager.cpp`
- ANSI 처리 후 실제 사용하지 않는 `StyledRun` 전달 객체 생성을 제거
- 출력 flood 시 redraw 요청을 50ms 단위로 coalescing

- `async_file_writer.cpp`, `async_file_writer.h`
- 비동기 로그 queue에 4MB 상한을 추가해 장시간 로그 폭주 시 메모리 증가 제한

- `utils.cpp`, `utils.h`
- 작은 파일 쓰기 유틸 파일을 기존 유틸 파일로 통합

## KTin 수정 66

- `terminal_view.cpp`
- 터미널 backbuffer bitmap을 paint마다 새로 만들지 않고 크기 변경 시에만 재생성하도록 변경
- 화면 행을 한 번만 읽어 row cache로 재사용하고 배경색 연속 구간을 span 단위로 칠하도록 렌더링 비용을 줄임

- `log_tail.cpp`
- 갈무리 보기 갱신 때 UI thread에서 로그 writer flush를 기다리던 경로를 제거
- tail refresh 중 디스크 flush 대기로 화면이 멈출 수 있는 가능성을 줄임

## KTin 수정 67

- `process_manager.cpp`
- 원본 ANSI 보관 버퍼 갱신을 bounded append로 정리해 버퍼 상한 초과 시 불필요한 반복 삭제 비용을 줄임

- `dialogs.cpp`, `terminal_buffer.cpp`
- 로그 찾기/텍스트 추출 경로에서 history row 실제 폭 기준 접근과 사전 reserve를 보강

- `input.cpp`
- 실행 중 입력 히스토리가 1000개를 넘으면 오래된 항목을 즉시 정리하도록 제한

## KTin tail/갈무리 효율 보강

- `log_tail.cpp`
- 갈무리 필터 문자열 분해 결과를 재사용해 줄 단위 필터링 비용 감소
- tail fragment 처리에서 반복 앞부분 삭제를 줄여 대량 로그 처리 비용 감소
- RichEdit 전체/선택 텍스트 복사 시 널 종료 공간을 포함해 버퍼 경계 안정성 보강
- 좌측 공백/필터 term trim 경로의 반복 erase 제거

## KTin 장시간 안정성 보강

- `theme.cpp`
- OSC GUI 변수 업데이트에서 과도한 payload와 PostMessage 실패 시 누수를 방어

- `dialogs.cpp`, `dialogs.h`, `main.cpp`
- 찾기 대화상자 공용 폰트를 종료 시 정리하도록 보강

## KTin 안정성/정합성 보강

- `app_state.h`
- 중복 멤버 선언을 제거하고 특수기호 창 핸들 기본값을 명시해 초기 상태 안정성 보강

- `terminal_buffer.cpp`, `terminal_buffer.h`
- 선언만 있던 보조 함수 구현을 맞추고 선택/화면 텍스트 추출 경계와 reserve를 보강

- `terminal_view.cpp`
- 중복 대입을 제거해 렌더링 경로를 정리

## KTin 입력 유실 방어

- `timer.cpp`
- `#TIMER`/`#TIMERGROUP` 내부 명령이 실제 타이머 항목과 액션을 처리했을 때만 입력을 가로채도록 변경
- 대상 없음/알 수 없는 액션의 명령이 조용히 유실되지 않고 기존 입력 경로로 전달되게 보강

## KTin 안정성/조각 정리 추가

- `about_dialog.h`, `help_dialog.h`
- `main.h`에 이미 있는 선언만 담은 조각 헤더를 제거

- `timer.cpp`
- 비정상 INI 값으로 타이머 항목/시간값이 과도하게 로드되지 않도록 상한과 범위 보정 추가

- `process_manager.cpp`, `terminal_buffer.cpp`
- 원본 ANSI bounded append와 화면 clear 시 history 보관 경로의 불필요한 복사/삭제 비용을 줄임

## KTin ANSI/UTF-8 경계 안정성 보강

- `theme.cpp`, `theme.h`
- 비정상적으로 긴 CSI/OSC escape sequence가 들어와도 내부 문자열 버퍼가 무제한 커지지 않도록 상한과 discard 상태를 추가
- UTF-8 decoder의 flush/append 경로에서 반복 erase와 불필요한 재할당 가능성을 줄임
## KTin 버퍼/텍스트 경계 보강

- `terminal_buffer.cpp`
- 화면/히스토리 텍스트 추출 경로를 공통 range append로 정리해 반복 문자열 복사 비용 감소
- live/history row 폭 불일치와 비정상 cells 크기 접근을 추가 방어
- scroll/clear 경로에서 cells 크기 불일치 시 안전하게 보정
- ANSI 숫자 인자 파싱 상한을 추가해 비정상 긴 수치 입력의 overflow 가능성 방어

- `process_manager.cpp`
- raw ANSI bounded buffer가 처음부터 최대 용량을 reserve하지 않도록 조정해 초기 메모리 점유 감소

- `theme.cpp`, `theme.h`
- ANSI 출력 append 경로에서 문자열 복사 1회를 제거

## KTin 안정성 추가 정리

- `log_tail.cpp`
- tail 필터 term 캐시 상한을 추가해 임시 패턴 변경 반복 시 캐시가 계속 늘어나는 경로를 방어
- 개행 없는 대용량 tail fragment에 상한을 적용해 장시간 실행 중 메모리 증가 가능성 감소

- `settings.cpp`
- 입력 히스토리가 상한을 넘은 상태로 저장될 때 최신 항목을 보존하도록 저장 범위 보정

- `input.cpp`
- 중복 include 정리

## KTin 성능/안정성 추가 보강

- `memo.cpp`
- 메모장 구문 강조에서 명령어/키워드 판정 시 반복 `substr` 할당을 제거해 대용량 문서 강조 비용 감소

- `theme.cpp`
- ANSI 입력 feed 경로에서 텍스트 버퍼 사전 reserve를 보강하고 빈 입력을 즉시 반환하도록 정리

- `process_manager.cpp`
- raw ANSI bounded buffer가 상한 도달 후 매 입력마다 앞부분을 삭제하지 않도록 headroom trim 적용

- `terminal_buffer.cpp`
- 비정상 좌표 접근용 fallback cell을 thread-local로 바꿔 보조 스레드 접근 시 공유 dummy 경합 가능성 감소

## KTin 컴파일/ANSI 경로 보강

- `terminal_buffer.cpp`, `terminal_buffer.h`, `terminal_view.cpp`, `memo.cpp`
- Windows `GetCharWidth` 매크로와 충돌하던 로컬 문자 폭 계산 함수명을 `KTinCharWidth`로 변경

- `theme.cpp`
- OSC GUI 변수 파싱에서 반복 `substr` 할당을 줄이고 `string_view` 기반으로 경로 정리

## KTin 핵심 성능 경로 보강

- `terminal_view.cpp`, `main.cpp`, `main.h`
- 출력 갱신을 dirty row 기반 invalidate로 바꾸고 paint 시 invalidated row 범위만 다시 그리도록 정리

- `terminal_buffer.cpp`, `terminal_buffer.h`, `dialogs.cpp`
- live screen을 row offset 기반 ring 구조로 전환해 `ScrollUp()`의 전체 `memmove` 제거

- `log_tail.cpp`
- tail 파일 읽기를 worker thread로 분리하고 UI thread는 완료된 chunk만 제한된 줄 수만큼 append하도록 변경
## KTin 핵심 경로 후속 보강

- `terminal_view.cpp`
- dirty row invalidation을 연속 row range 단위로 합쳐 `InvalidateRect` 호출 수를 줄임
- paint 중 row cell 임시 버퍼를 재사용해 repaint 반복 할당 감소

- `terminal_buffer.cpp`
- ring row `ScrollUp()`에서 history row 복사를 `copy_n` 기반으로 정리하고 비정상 cells 크기 경계를 추가 방어

- `log_tail.cpp`
- worker가 넘긴 tail chunk를 UI thread에서 append할 때 RichEdit redraw를 tick 단위로 묶어 갱신 비용 감소
## KTin UI 장시간 안정성 보강

- `terminal_buffer.cpp`, `terminal_buffer.h`
- `AppendText()` chunk append 경로를 추가해 ANSI parser 출력이 문자마다 lock/unlock하지 않도록 정리
- `PutCharUnlocked()` / `ScrollUpUnlocked()`를 분리하고 TerminalBuffer 잠금을 `std::mutex` 기반 단일 잠금 구조로 정리
- 화면 렌더링용 `TerminalRenderSnapshot` 생성 경로를 추가해 paint가 필요한 cell/selection 상태만 짧게 복사하도록 변경

- `terminal_view.cpp`
- `WM_PAINT`가 TerminalBuffer lock을 잡은 상태로 GDI drawing을 수행하던 구조를 snapshot 기반 렌더링으로 변경
- selection 판정도 snapshot 기준으로 처리해 paint 중 TerminalBuffer 재진입을 제거

- `theme.cpp`
- ANSI run append를 문자 단위 `PutChar()` 호출에서 chunk 단위 `AppendText()` 호출로 변경

## KTin 수정 81

- `terminal_view.cpp`, `terminal_buffer.cpp`, `terminal_buffer.h`
- `WM_PAINT` snapshot 범위를 실제 paint row로 제한해 repaint 중 복사량을 축소
- backbuffer paint 영역을 먼저 배경색으로 정리해 부분 repaint 잔상 가능성 감소
- 스크롤/선택 좌표 계산 일부를 TerminalBuffer lock 내부 helper로 이동

- `log_tail.cpp`
- 공통 tail timer가 한 tick에 처리하는 갈무리창 수를 제한해 다중 tail 창에서 UI thread 점유를 완화
- tail 창 종료 후 timer cursor 보정과 중복 호출 정리
## KTin 수정 82

- `terminal_buffer.cpp`, `terminal_buffer.h`
- dirty row public API가 자체 lock을 잡도록 정리하고 내부 경로는 unlocked helper로 분리
- `ScrollUpUnlocked()`의 중복 지역 변수 선언을 제거해 컴파일 실패 가능성 수정
- scroll top/live 이동과 selection range 설정을 public 함수로 분리해 외부 코드의 직접 상태 변경을 줄임

- `dialogs.cpp`, `input.cpp`, `utils.cpp`, `theme.cpp`
- 로그 검색 중 `std::mutex` 재진입 deadlock 가능성을 제거
- 스크롤/선택/테마 변경 시 dirty row 상태를 일관되게 갱신

## KTin 수정 83

- `terminal_view.cpp`, `utils.cpp`
- 터미널 row invalidate와 마우스 좌표 계산에서 `TerminalBuffer` 크기 값을 직접 읽지 않고 metrics snapshot을 사용하도록 정리
- `WM_PAINT` 진입 시 크기 확인용 snapshot cell 복사를 제거해 paint 전 잠금 시간을 추가 축소

## KTin 수정 85

- `terminal_buffer.cpp`, `terminal_buffer.h`
- 외부 직접 접근이 남아 있던 TerminalBuffer 상태를 private 영역으로 이동하고 snapshot/API 경로로 정리
- 로그 검색용 snapshot과 선택 위치 이동 API를 추가해 검색 중 장시간 buffer lock 점유를 줄임
- 로그 삭제와 테마 색상 갱신을 TerminalBuffer 내부 API로 캡슐화

- `dialogs.cpp`, `main.cpp`, `input.cpp`, `theme.cpp`
- history/size/selection/theme 변경 경로의 직접 상태 접근을 public API 호출로 교체

## KTin 수정 86

- `terminal_buffer.cpp`, `terminal_buffer.h`
- 로그 검색용 전체 cell snapshot 생성을 제거하고 row 단위 텍스트 snapshot API로 변경

- `dialogs.cpp`
- 로그 찾기를 전체 history 복사/검색 방식에서 row 단위 검색으로 바꿔 대형 로그에서 UI lock 점유와 메모리 폭증을 완화

## KTin 수정 87

- `terminal_buffer.cpp`, `terminal_buffer.h`
- public cursor/cell helper가 `std::mutex` 없이 TerminalBuffer 상태를 수정할 수 있던 경로를 lock wrapper와 unlocked 내부 helper로 분리
- `PutChar()` / ANSI command 처리 중 내부 helper 재사용으로 재진입 없이 같은 lock 안에서 cursor/clear 처리를 수행하도록 정리

## KTin 수정 91

- `terminal_view.cpp`
- `WM_PAINT`에서 기본 배경 row/span 재도색을 줄이고 비기본 배경 span만 그리도록 정리
- snapshot row를 포인터로 직접 순회해 cell 조회 반복 비용을 줄임
- 선택 영역 렌더링을 row별 selected range/span 단위로 바꿔 full row scan과 `InvertRect` 반복을 축소

## KTin 수정 95

- `terminal_buffer.cpp`
- 선택 복사/화면 복사/전체 history 복사에서 문자열 조립을 TerminalBuffer lock 밖으로 이동
- 대형 로그 복사/저장 시 입력 reader와 paint 경로의 lock 대기 시간을 줄임

- `terminal_view.cpp`
- row invalidate/paint offset 계산에서 metrics/cell 재조회 중복을 줄이는 layout helper 추가
- dirty row invalidate와 WM_PAINT의 offset 계산 비용을 축소

## KTin 수정 96

- `theme.cpp`, `terminal_buffer.cpp`
- 테마 변경 시 history/live cell recolor에서 테마 배경색 목록을 셀마다 재계산하던 경로를 1회 캐시로 변경
- 동일 색상 테마 재적용 시 전체 history 스캔을 건너뛰도록 보강

- `terminal_view.cpp`
- 마우스 selection/cursor hit-test에서 metrics/offset 계산과 `ViewRowToAbsRow()` lock 재조회 경로를 줄임
- 드래그 selection 중 반복 lock/좌표 계산 비용을 완화
- 단어 hit-test용 `GetWordAt()` 문자열 조립을 lock 밖으로 이동


## KTin 수정 97

- `utils.cpp`, `utils.h`, `theme.cpp`
- 로그 터미널 cell pixel size 계산을 font/window/DPI 기준으로 캐시해 paint/invalidate/mouse hit-test 중 반복 GDI 측정을 줄임
- 로그 폰트 변경 시 cell size cache를 즉시 무효화하도록 보강
- 테마/폰트 적용 후 강제 erase repaint를 줄여 흰색 깜빡임 가능성을 완화

- `terminal_view.cpp`
- WM_PAINT에서 paint 영역이 터미널 grid와 겹치지 않으면 snapshot 생성과 row 순회를 생략
- cursor word hit-test에 짧은 캐시를 추가해 같은 위치에서 반복되는 `GetWordAt()` lock/row snapshot 비용을 줄임

## KTin 수정 98

- `terminal_buffer.cpp`, `terminal_buffer.h`
- row text snapshot에 column→char index 매핑을 추가하고 `HasWordAt()` 경로를 분리
- 커서 hit-test가 단어 전체 문자열을 만들지 않고 존재 여부만 확인하도록 최적화

- `terminal_view.cpp`
- 터미널 grid 밖 `WM_SETCURSOR`에서 단어 검색/TerminalBuffer lock을 수행하지 않도록 차단
- 마우스가 여백에 있을 때 가장자리 cell로 clamp되어 불필요하게 hand cursor가 뜨는 경로 완화

- `log_tail.cpp`
- tail 폰트/레이아웃 갱신 후 erase repaint를 줄여 갈무리창 갱신 시 흰색 깜빡임 가능성 완화

## KTin 수정 a01

- `main.cpp`, `app_state.h`, `constants.h`
- 지난 화면 복사/저장을 UI thread 직접 처리에서 worker snapshot 생성/파일 저장 후 UI 완료 통지 방식으로 변경
- 대형 history 복사/저장 시 문자열 생성과 파일 I/O로 인한 UI 멈춤 가능성을 완화
- `TerminalBuffer`를 `shared_ptr`로 보관해 비동기 history 작업 중 lifetime을 보장

- `app_version.h`
- 2.6.99 이후 버전 표기를 `2.6-a01` 형식으로 전환

## ktin_fix_a02 / 2.6-a02

변경 파일:
- terminal_buffer.h
- terminal_buffer.cpp
- terminal_view.cpp
- theme.cpp
- constants.h
- highlight.cpp
- settings.cpp
- main.cpp
- app_version.h
- Changelog_magun.md

핵심 변경:
- 테마 변경 시 전체 live/history recolor를 UI thread에서 분리
- 테마 기본색은 즉시 반영하고 기존 셀 recolor만 background worker 처리
- 연속 테마 변경 시 오래된 recolor worker가 뒤늦게 적용되지 않도록 serial guard 추가
- ClearLogWindow / highlight / screen settings erase repaint를 FALSE로 축소
- terminal row invalidate 중복 layout 호출 제거
- 버전 표기 2.6-a02 적용

# KTin 2.6-a03

- log_tail.cpp: 완료된 tail worker의 UI thread join 경로를 background retire 처리로 변경.
- log_tail.cpp: 새 tail read worker 시작 전 joinable 완료 thread가 남아 있을 때 UI blocking 없이 정리.
- dialogs.cpp: 찾기 선택 갱신 후 erase repaint를 사용하지 않도록 변경.
- app_version.h: 버전 2.6-a03 적용.

## KTin 수정 a04

- `utils.cpp`
- alpha 버전 빌드에서 사용되지 않는 `FormatKtinVersion()` 컴파일 경고를 조건부 컴파일로 제거

- `app_version.h`
- 버전 표기를 `2.6-a04`로 갱신

