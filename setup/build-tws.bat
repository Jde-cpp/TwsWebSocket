Set TwsPath=C:\TWS API\source\CppClient\client
Set mypath=%~dp0
echo %mypath:~0,-1%
copy "%mypath:~0,-1%\TwsSocketClient64.vcxproj" "%TwsPath%\TwsSocketClient64.vcxproj"
Set SubDir=BuildTools
rem echo done
set x="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%SubDir%\MSBuild\Current\Bin\"
rem echo x;
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%SubDir%\MSBuild\Current\Bin\" ( set SubDir=Enterprise)

"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%SubDir%\MSBuild\Current\Bin\MSBuild.exe" "%TwsPath%\TwsSocketClient64.vcxproj"  -p:Configuration=Release -p:Platform=x64
copy "%TwsPath%\.bin\Release" "%mypath:~0,-1%"