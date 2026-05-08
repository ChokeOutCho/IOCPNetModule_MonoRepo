@echo off
echo [1/3] 서버들을 순차적으로 실행..

:: /d 옵션을 사용하여 exe가 있는 폴더를 작업 디렉터리로 강제 지정합니다.
start /d "bin\x64\Release" "" "LoginServer.exe"
timeout /t 1 /nobreak >nul

start /d "bin\x64\Release" "" "ChatServer.exe"
timeout /t 1 /nobreak >nul

start /d "bin\x64\Release" "" "MonitorServer.exe"
timeout /t 1 /nobreak >nul

echo.
echo [2/3] 모니터링 클라이언트 실행..
start /d "Clients\MonitoringClient_20241225" "" "MonitoringClient.exe"

echo [3/3] 더미 클라이언트를 새 창으로 실행..
start /d "Clients\ChatDummy_Loginserver_20221114" "" "ChatDummy_Login_20221114.exe"

echo.
echo 모든 프로세스 호출 완료
pause