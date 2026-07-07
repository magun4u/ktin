@echo off
setlocal

if exist *.obj del /q *.obj
if exist ktin.exe del /q ktin.exe
if exist Resource.res del /q Resource.res

rc /nologo /fo Resource.res Resource.rc
if errorlevel 1 goto fail

cl /utf-8 /std:c++17 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:ktin.exe *.cpp Resource.res user32.lib gdi32.lib shell32.lib comdlg32.lib comctl32.lib uxtheme.lib winmm.lib dwmapi.lib version.lib
if errorlevel 1 goto fail

echo.
echo 빌드 성공: ktin.exe
goto end

:fail
echo.
echo 빌드 실패. 위 오류 내용을 복사해서 보내주세요.

:end
pause
