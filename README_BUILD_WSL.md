KTin WSL 빌드
===================

WSL에서 MinGW-w64 교차 컴파일러로 Windows용 ktin.exe를 만든다.

1. WSL에 빌드 도구 설치

   sudo apt update
   sudo apt install -y build-essential mingw-w64

2. 빌드

   cd ~/ktin
   make

3. 결과 파일

   ktin.exe

4. 실행 시 DLL 오류가 날 때

   libgcc_s_seh-1.dll 또는 libstdc++-6.dll 오류는 MinGW 런타임 DLL을
   찾지 못해서 생긴다.
   이 Makefile은 해당 런타임을 정적 링크하도록 설정.
   기존 ktin.exe가 있다면 make clean 후 다시 빌드.

5. 리소스 빌드가 실패할 때

   make clean
   make nores

6. 컴파일러 확인

   x86_64-w64-mingw32-g++ --version
   x86_64-w64-mingw32-windres --version

7. 빌드 산출물 정리

   make clean

   clean은 ktin.exe, *.o, *.obj, *.res, *.aps 파일을 삭제한다.
