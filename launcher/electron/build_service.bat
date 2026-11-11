@echo off
setlocal
rem Builds NightFireLauncherService.exe (and AgentUnderFireLauncherService.exe, below) (sdk/launcher's builder driven over stdin/stdout, launcher\game.json's game)
rem into out\launcher-svc, where `npm start` and `npm run dist` take it. Needs what build.bat needs.
cd /d "%~dp0\..\.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with the C++ workload was not found: run build.bat once, it sets it up.
    exit /b 1
)
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%VSDIR%\VC\Tools\Llvm\x64\bin;%ProgramFiles%\LLVM\bin;%PATH%"
cmake -S sdk\launcher -B out\launcher-svc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLAUNCHER_GAME="%CD%\launcher" || exit /b 1
cmake --build out\launcher-svc --target launcher_service || exit /b 1
echo BUILD OK: out\launcher-svc\NightFireLauncherService.exe
rem Agent Under Fire's service (its own launcher core, the AUF repository's sdk\launcher and launcher\game.json) into
rem out\launcher-svc-auf: AUF_REPO names that repository's folder
if not defined AUF_REPO (
    echo AUF_REPO is not set: the launcher is made without Agent Under Fire's service.
    exit /b 0
)
cmake -S "%AUF_REPO%\sdk\launcher" -B out\launcher-svc-auf -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLAUNCHER_GAME="%AUF_REPO%\launcher" || exit /b 1
cmake --build out\launcher-svc-auf --target launcher_service || exit /b 1
echo BUILD OK: out\launcher-svc-auf\AgentUnderFireLauncherService.exe
