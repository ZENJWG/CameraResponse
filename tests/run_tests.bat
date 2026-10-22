@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cl.exe not found. Use a Visual Studio Developer Command Prompt.
    exit /b 1
)
cl /nologo /std:c++17 /EHsc /W4 response_core_tests.cpp /Fe:response_core_tests.exe
if errorlevel 1 exit /b 1
response_core_tests.exe
exit /b %errorlevel%
