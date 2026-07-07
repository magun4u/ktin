@echo off
setlocal

if exist *.obj del /q *.obj
if exist ktin.exe del /q ktin.exe
if exist Resource.res del /q Resource.res

set RCDEFS=
if exist MudDunggeunmo-Regular.ttf (
    echo [KTin] MudDunggeunmo-Regular.ttf found. Building with embedded Mud font.
    set RCDEFS=/d HAVE_EMBEDDED_FONT
) else (
    echo [KTin] MudDunggeunmo-Regular.ttf not found. Building without embedded Mud font.
    echo [KTin] You can still place MudDunggeunmo-Regular.ttf next to ktin.exe for private runtime loading.
)

rc /nologo %RCDEFS% /fo Resource.res Resource.rc
if errorlevel 1 goto fail

cl /utf-8 /std:c++17 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:ktin.exe *.cpp Resource.res user32.lib gdi32.lib shell32.lib comdlg32.lib comctl32.lib uxtheme.lib winmm.lib dwmapi.lib version.lib
if errorlevel 1 goto fail

echo.
echo Build success: ktin.exe
goto end

:fail
echo.
echo Build failed. Please check the error messages above.

:end
pause
