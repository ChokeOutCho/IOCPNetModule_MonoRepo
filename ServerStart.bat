@echo off
echo [1/4] 데이터베이스 및 캐시 서버를 구동합니다 (Docker)...
docker-compose up -d

if %errorlevel% neq 0 (
    echo.
    echo Docker 구동에 실패했습니다. 
    echo Docker Desktop이 실행 중인지, 포트가 충돌하지 않는지 확인해 주세요.
    pause
    exit /b
)

echo.
echo 데이터베이스 초기화 대기 중 (15초)...
timeout /t 15 /nobreak >nul



echo.
echo [2/4] 서버들을 순차적으로 실행합니다...

start /d "bin\x64\Release" "" "MonitorServer.exe"
echo 모니터링 서버 실행 완료. 대기 중 (1초)...
timeout /t 1 /nobreak >nul

start /d "bin\x64\Release" "" "LoginServer.exe"
echo 로그인 서버 실행 완료. 대기 중 (1초)...
timeout /t 1 /nobreak >nul

start /d "bin\x64\Release" "" "ChatServer.exe"
echo 채팅 서버 실행 완료. 대기 중 (1초)...
timeout /t 1 /nobreak >nul

set "all_clear=1"

tasklist /FI "IMAGENAME eq LoginServer.exe" 2>NUL | find /I /N "LoginServer.exe">NUL
if "%ERRORLEVEL%"=="0" ( echo [ O K ] LoginServer 정상 작동 중 ) else ( echo [FAIL] LoginServer 실행 실패! & set "all_clear=0" )

tasklist /FI "IMAGENAME eq ChatServer.exe" 2>NUL | find /I /N "ChatServer.exe">NUL
if "%ERRORLEVEL%"=="0" ( echo [ O K ] ChatServer 정상 작동 중 ) else ( echo [FAIL] ChatServer 실행 실패! & set "all_clear=0" )

tasklist /FI "IMAGENAME eq MonitorServer.exe" 2>NUL | find /I /N "MonitorServer.exe">NUL
if "%ERRORLEVEL%"=="0" ( echo [ O K ] MonitorServer 정상 작동 중 ) else ( echo [FAIL] MonitorServer 실행 실패! & set "all_clear=0" )

if "%all_clear%"=="0" (
    echo.
    echo [ERROR] 하나 이상의 서버가 구동에 실패했습니다. DB 연결 설정이나 포트를 확인해 주세요.
    pause
    exit /b
)

echo.
echo [3/4] 모니터링 클라이언트를 실행합니다...
start /d "Clients\MonitoringClient_20241225" "" "MonitoringClient.exe"

echo.
echo [4/4] 더미 클라이언트를 새 창으로 띄웁니다. (번호 직접 입력 필요)
start /d "Clients\ChatDummy_Loginserver_20221114" "" "ChatDummy_Login_20221114.exe"

echo.
echo 모든 프로세스가 성공적으로 호출되었습니다
pause