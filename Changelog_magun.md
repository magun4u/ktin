# changelog_magun

## WSL / MinGW 빌드 수정

- `Makefile`
  - WSL MinGW 빌드 규칙 추가.
  - 리소스 UTF-8 코드페이지 지정.
  - MinGW 런타임 정적 링크 적용.
  - `make nores` 리소스 제외 빌드 경로 추가.

- `README_BUILD_WSL.txt`
  - WSL 빌드 안내와 DLL 오류 대응을 한국어로 정리.

- `Resource.rc`
  - UTF-16LE를 UTF-8로 변환해 `windres` null 문자 경고/전처리 오류 수정.

- `dialogs.cpp`, `dialogs.h`
  - 내부 함수 선언 충돌과 WinAPI 구조체 초기화 경고 정리.

- `settings.cpp`, `settings.h`
  - 설정 미리보기 선언 오류와 미사용/들여쓰기 경고 정리.

- `main.cpp`, `main.h`
  - MinGW 선언 충돌, MSVC 전용 pragma, WinAPI 초기화 경고 정리.

- `memo.cpp`, `memo.h`
  - `fstream` 경로 오류, 선언 충돌, 미사용 함수/파라미터 경고 정리.

- `highlight.cpp`, `highlight.h`
  - 선언 충돌과 구조체 초기화 경고 정리.

- `input.cpp`, `input.h`
  - 내부 선언 충돌과 미사용 변수 경고 정리.

- `shortcut_bar.h`, `status_bar.cpp`, `status_bar.h`
  - 프로시저 선언 충돌과 미사용 파라미터 경고 정리.

- `chat_capture.h`
  - 플로팅 창 프로시저 선언 충돌 정리.

- `address_book.cpp`, `auto_login.cpp`, `help_dialog.cpp`, `log_tail.cpp`, `terminal_buffer.cpp`, `timer.cpp`
  - 미사용 변수/함수, 들여쓰기, 조건식 경고 정리.

- `utils.cpp`, `utils.h`
  - 선언 충돌, 미사용 파라미터, 초기화 경고 정리.

## 소스 정리

- `Makefile`
  - `make clean`에서 `*.o`, `*.obj`, `*.res`, `*.aps`, `ktin.exe`를 함께 삭제하도록 정리.

- `.gitignore`
  - 빌드 산출물, 로컬 설정 파일, 실행 파일, `bin/` 디렉토리를 제외하도록 추가.

- 저장소 정리
  - 빌드 산출물과 개인 설정 파일을 소스 패키지에서 제외.
