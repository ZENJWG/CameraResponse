@echo off
setlocal EnableExtensions EnableDelayedExpansion

title GTASA: Camera Response v0.9 - Build

rem Always switch to the folder containing this batch.
cd /d "%~dp0"
for %%I in (.) do set "ROOT=%%~fI"

set "DEPS=!ROOT!\deps"
set "LOCAL_SDK=!DEPS!\plugin-sdk"
set "DIST=!ROOT!\dist"
set "BUILD=!ROOT!\build"
set "PLUGIN_SDK_REV=a1df3a6"

echo.
echo ============================================================
echo   GTASA: Camera Response v0.9 - one-click Release build
echo   Target: GTA San Andreas Classic PC US 1.0 / x86

echo   Made by Zeneric
echo ============================================================
echo.
echo [INFO] Project directory:
echo        !ROOT!
echo.

rem ============================================================
rem 1. Locate plugin-sdk
rem ============================================================

set "SDK_DIR="

if not defined PLUGIN_SDK_DIR goto :check_local_sdk
if not exist "!PLUGIN_SDK_DIR!\tools\premake\premake5.exe" goto :bad_env_sdk

set "SDK_DIR=!PLUGIN_SDK_DIR!"
echo [OK] Using PLUGIN_SDK_DIR:
echo      !SDK_DIR!
goto :sdk_found

:bad_env_sdk
echo [INFO] PLUGIN_SDK_DIR does not contain the required premake5.exe.
echo [INFO] Falling back to deps\plugin-sdk.
goto :check_local_sdk

:check_local_sdk
if not exist "!LOCAL_SDK!\tools\premake\premake5.exe" goto :download_sdk

set "SDK_DIR=!LOCAL_SDK!"
echo [OK] Using existing local plugin-sdk:
echo      !SDK_DIR!
goto :sdk_found

:download_sdk
echo [INFO] plugin-sdk was not found.
echo [INFO] Downloading pinned DK22Pac/plugin-sdk revision !PLUGIN_SDK_REV!...
echo.

if not exist "!DEPS!" mkdir "!DEPS!"

where git.exe >nul 2>nul
if errorlevel 1 goto :download_zip

echo [INFO] Git found. Cloning plugin-sdk...
git clone https://github.com/DK22Pac/plugin-sdk.git "!LOCAL_SDK!"
if errorlevel 1 goto :git_failed
pushd "!LOCAL_SDK!"
git checkout --detach !PLUGIN_SDK_REV!
set "SDK_CHECKOUT_ERR=!ERRORLEVEL!"
popd
if not "!SDK_CHECKOUT_ERR!"=="0" goto :git_failed
goto :verify_download

:git_failed
echo [WARN] Git clone failed. Falling back to PowerShell ZIP download.
goto :download_zip

:download_zip
set "SDK_ZIP_FOR_PS=!DEPS!\plugin-sdk-!PLUGIN_SDK_REV!.zip"
set "DEPS_FOR_PS=!DEPS!"
set "LOCAL_SDK_FOR_PS=!LOCAL_SDK!"
if exist "!SDK_ZIP_FOR_PS!" del /q "!SDK_ZIP_FOR_PS!" >nul 2>nul
if exist "!LOCAL_SDK!" rmdir /s /q "!LOCAL_SDK!" >nul 2>nul

echo [INFO] Downloading plugin-sdk ZIP with PowerShell...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; $u='https://github.com/DK22Pac/plugin-sdk/archive/' + $env:PLUGIN_SDK_REV + '.zip'; Invoke-WebRequest -UseBasicParsing $u -OutFile $env:SDK_ZIP_FOR_PS; $tmp=Join-Path $env:DEPS_FOR_PS 'plugin-sdk-extract'; if(Test-Path $tmp){Remove-Item -Recurse -Force $tmp}; New-Item -ItemType Directory -Path $tmp | Out-Null; Expand-Archive -LiteralPath $env:SDK_ZIP_FOR_PS -DestinationPath $tmp -Force; $d=Get-ChildItem $tmp -Directory | Select-Object -First 1; if(-not $d){throw 'Archive did not contain plugin-sdk directory'}; Move-Item $d.FullName $env:LOCAL_SDK_FOR_PS; Remove-Item -Recurse -Force $tmp"
if errorlevel 1 goto :sdk_download_failed

del /q "!SDK_ZIP_FOR_PS!" >nul 2>nul
goto :verify_download

:sdk_download_failed
echo [ERROR] Could not download/extract plugin-sdk.
goto :fail

:verify_download
if not exist "!LOCAL_SDK!\tools\premake\premake5.exe" goto :sdk_download_bad

set "SDK_DIR=!LOCAL_SDK!"
echo [OK] plugin-sdk downloaded successfully.
goto :sdk_found

:sdk_download_bad
echo [ERROR] plugin-sdk exists, but this file is missing:
echo         !LOCAL_SDK!\tools\premake\premake5.exe
goto :fail

:sdk_found
rem Verify the SDK revision when this is a Git checkout. External SDKs are
rem allowed, but a mismatch is reported because class/header changes can break
rem a binary plugin even when compilation succeeds.
if exist "!SDK_DIR!\.git" (
    where git.exe >nul 2>nul
    if not errorlevel 1 (
        pushd "!SDK_DIR!"
        for /f "delims=" %%H in ('git rev-parse --short HEAD 2^>nul') do set "SDK_HEAD=%%H"
        popd
        if defined SDK_HEAD (
            echo [INFO] plugin-sdk revision: !SDK_HEAD! ^(tested: !PLUGIN_SDK_REV!^)
            echo !SDK_HEAD! | findstr /b /i "!PLUGIN_SDK_REV!" >nul
            if errorlevel 1 echo [WARN] SDK revision differs from the tested baseline.
        )
    )
)
set "PLUGIN_SDK_DIR=!SDK_DIR!"
set "SDK_PREMAKE=!SDK_DIR!\tools\premake\premake5.exe"
set "SDK_PREMAKE_DIR=!SDK_DIR!\tools\premake"

echo.
echo [OK] Plugin-SDK:
echo      !SDK_DIR!

rem ============================================================
rem 2. Locate MSBuild
rem ============================================================

set "MSBUILD="
set "VSWHERE=!ProgramFiles(x86)!\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "!VSWHERE!" goto :find_msbuild_on_path

for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%I"

if defined MSBUILD goto :msbuild_found

:find_msbuild_on_path
for /f "delims=" %%I in ('where MSBuild.exe 2^>nul') do set "MSBUILD=%%I"

if defined MSBUILD goto :msbuild_found

echo.
echo [ERROR] MSBuild with Visual C++ x86 support was not found.
echo.
echo Install Visual Studio 2022 or newer with:
echo   Desktop development with C++
echo   MSVC x86/x64 build tools
echo   Windows SDK
echo.
goto :fail

:msbuild_found
echo [OK] MSBuild:
echo      !MSBUILD!

rem ============================================================
rem 3. Make sure plugin-sdk Plugin.lib exists
rem ============================================================

if exist "!SDK_DIR!\output\lib\Plugin.lib" goto :sdk_lib_ready
if exist "!SDK_DIR!\output\lib\plugin.lib" goto :sdk_lib_ready

echo.
echo [INFO] plugin-sdk Plugin.lib is missing.

if exist "!SDK_DIR!\plugin_sa\plugin_sa.vcxproj" goto :build_sdk_lib

echo [INFO] Generating plugin-sdk Visual Studio projects...
pushd "!SDK_PREMAKE_DIR!"
premake5.exe vs2022 --pluginsdkdir="!SDK_DIR!"
set "SDK_GEN_ERR=!ERRORLEVEL!"
popd

if not "!SDK_GEN_ERR!"=="0" goto :sdk_generate_failed
if not exist "!SDK_DIR!\plugin_sa\plugin_sa.vcxproj" goto :sdk_project_missing

:build_sdk_lib
echo [INFO] Building plugin-sdk GTA SA Release library...
"!MSBUILD!" "!SDK_DIR!\plugin_sa\plugin_sa.vcxproj" /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=Win32
if errorlevel 1 goto :sdk_build_failed

if exist "!SDK_DIR!\output\lib\Plugin.lib" goto :sdk_lib_ready
if exist "!SDK_DIR!\output\lib\plugin.lib" goto :sdk_lib_ready

echo [ERROR] plugin-sdk build finished but Plugin.lib is missing.
goto :fail

:sdk_generate_failed
echo [ERROR] plugin-sdk Visual Studio project generation failed.
goto :fail

:sdk_project_missing
echo [ERROR] plugin-sdk generation did not create:
echo         !SDK_DIR!\plugin_sa\plugin_sa.vcxproj
goto :fail

:sdk_build_failed
echo [ERROR] plugin-sdk GTA SA library build failed.
goto :fail

:sdk_lib_ready
echo [OK] plugin-sdk GTA SA library is ready.

rem ============================================================
rem 4. Generate CameraResponse solution
rem ============================================================

echo.
echo [INFO] Running configuration audit...
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0tools\config_audit.py"
    if errorlevel 1 (
        echo [ERROR] CameraResponse.ini/source configuration audit failed.
        exit /b 1
    )
) else (
    where python >nul 2>nul
    if not errorlevel 1 (
        python "%~dp0tools\config_audit.py"
        if errorlevel 1 (
            echo [ERROR] CameraResponse.ini/source configuration audit failed.
            exit /b 1
        )
    ) else (
        echo [INFO] Python not found; skipping optional configuration audit.
    )
)
echo.

echo [INFO] Running performance/static audit...
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0tools\performance_audit.py"
) else (
    where python >nul 2>nul
    if not errorlevel 1 (
        python "%~dp0tools\performance_audit.py"
    ) else (
        echo [INFO] Python not found; skipping optional performance/static audit.
    )
)
echo.

echo [INFO] Generating CameraResponse Visual Studio project...

if not exist "!BUILD!" mkdir "!BUILD!"

rem Remove stale Premake output so an older GTA-SA-platform solution cannot
rem survive after changing generator settings.
if exist "!BUILD!\CameraResponse.sln" del /q "!BUILD!\CameraResponse.sln" >nul 2>nul
if exist "!BUILD!\CameraResponse.vcxproj" del /q "!BUILD!\CameraResponse.vcxproj" >nul 2>nul
if exist "!BUILD!\CameraResponse.vcxproj.filters" del /q "!BUILD!\CameraResponse.vcxproj.filters" >nul 2>nul

"!SDK_PREMAKE!" --file="!ROOT!\premake5.lua" vs2022 --pluginsdkdir="!SDK_DIR!"
if errorlevel 1 goto :cameraresponse_generate_failed

if not exist "!BUILD!\CameraResponse.sln" goto :cameraresponse_solution_missing
goto :cameraresponse_solution_ready

:cameraresponse_generate_failed
echo [ERROR] CameraResponse project generation failed.
goto :fail

:cameraresponse_solution_missing
echo [ERROR] Expected solution was not generated:
echo         !BUILD!\CameraResponse.sln
goto :fail

:cameraresponse_solution_ready

rem ============================================================
rem 5. Compile Release / Win32
rem ============================================================

echo.
echo [INFO] Building CameraResponse.asi...

"!MSBUILD!" "!BUILD!\CameraResponse.sln" /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=Win32
if errorlevel 1 goto :cameraresponse_build_failed

if not exist "!ROOT!\bin\CameraResponse.asi" goto :cameraresponse_binary_missing
goto :collect_output

:cameraresponse_build_failed
echo [ERROR] CameraResponse compilation failed.
goto :fail

:cameraresponse_binary_missing
echo [ERROR] Build reported success, but this file is missing:
echo         !ROOT!\bin\CameraResponse.asi
goto :fail

rem ============================================================
rem 6. Collect final files
rem ============================================================

:collect_output
if not exist "!DIST!" mkdir "!DIST!"

copy /y "!ROOT!\bin\CameraResponse.asi" "!DIST!\CameraResponse.asi" >nul
if errorlevel 1 goto :copy_failed

copy /y "!ROOT!\CameraResponse.ini" "!DIST!\CameraResponse.ini" >nul
if errorlevel 1 goto :copy_failed

echo.
echo ============================================================
echo   BUILD SUCCEEDED
echo ============================================================
echo.
echo Output:
echo   !DIST!\CameraResponse.asi
echo   !DIST!\CameraResponse.ini
echo.
echo Replace BOTH installed files with the versions from dist.
echo.

if not defined CI (
    explorer "!DIST!" >nul 2>nul
    pause
)
exit /b 0

:copy_failed
echo [ERROR] Could not copy output files into dist.
goto :fail

:fail
echo.
echo ============================================================
echo   BUILD FAILED
echo ============================================================
echo.
echo Scroll up to the first [ERROR] line.
echo.
if not defined CI pause
exit /b 1
