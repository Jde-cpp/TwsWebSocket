Set TwsPath=C:\TWS API\source\CppClient\client
Set SubDir=BuildTools
echo done
set x="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%SubDir%\MSBuild\Current\Bin\"
echo x;
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%SubDir%\MSBuild\Current\Bin\" ( set SubDir=Enterprise)

"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%SubDir%\MSBuild\Current\Bin\MSBuild.exe" "%TwsPath%\TwsSocketClient64.vcxproj"  -p:Configuration=Release -p:Platform=x64
copy "%TwsPath%\.bin\Release" .