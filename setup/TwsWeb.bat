Set ApplicationPath="%ProgramFiles%\jde-cpp\TwsWebSocket\TwsWebSocket.exe"
cmd /min /C start "" %ApplicationPath% -c
cd %ProgramData%\Jde-cpp\web\
http-server ./TwsWebsite/