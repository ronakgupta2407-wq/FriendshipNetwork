@echo off
echo.
echo  BondUp - Friendship Network
echo  Building C++ Backend...
echo.

g++ -std=c++14 -O2 main.cpp -lws2_32 -o server.exe

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  BUILD FAILED! Make sure g++ is installed and in PATH.
    echo.
    pause
    exit /b 1
)

echo  BUILD SUCCESSFUL!
echo.
echo  Run: server.exe
echo  Then open: http://localhost:8080/
echo.
pause
