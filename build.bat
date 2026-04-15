@echo off
echo ============================================
echo   BondUp C++ Backend - Build Script
echo ============================================
echo.
echo Compiling main.cpp ...
g++ -std=c++17 -O2 main.cpp -lws2_32 -o server.exe
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)
echo.
echo [OK] Build successful! server.exe created.
echo.
echo To run:  server.exe
echo Then open:  http://localhost:8080/
echo.
pause
