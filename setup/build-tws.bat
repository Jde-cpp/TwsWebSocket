Set TwsPath=C:\TWS API\source\CppClient\client
copy TwsSocketClient64.vcxproj "%TwsPath%"

"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "%TwsPath%\TwsSocketClient64.vcxproj"  -p:Configuration=Release -p:Platform=x64
copy "%TwsPath%\.bin\Release" .