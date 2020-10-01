Set ApplicationPath="%ProgramFiles%\jde-cpp\JdeTwsWebSocket\TwsWebSocket.exe"
cmd /min /C start "" %ApplicationPath% -c
cd %ProgramData%\Jde-cpp\web\
http-server ./TwsWebsite/