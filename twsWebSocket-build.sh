#git clone https://gitlab.com/libeigen/eigen.git
#git clone https://github.com/gabime/spdlog.git

#export INCLUDE=$INCLUDE\;C:\\Users\\duffyj\\source\\repos\\eigen
#set $PATH=$PATH:C:\Users\duffyj\source\repos\boost_1_72_0
#cd C:\Users\duffyj\source\repos\boost_1_72_0
#bootstrap
#b2 variant=debug link=shared threading=multi runtime-link=shared address-model=64 --with-date_time
#b2 variant=release link=shared threading=multi runtime-link=shared address-model=64 --with-date_time
#b2 variant=debug link=shared threading=multi runtime-link=shared address-model=64 --with-regex
#b2 variant=release link=shared threading=multi runtime-link=shared address-model=64 --with-regex
#Set in View\Other Windows\Property Manager\Microsoft.Cpp.x64.user
# git clone https://github.com/johnduffynh/tws-api.git
# cd tws-api/
# git remote add upstream https://github.com/InteractiveBrokers/tws-api.git
# git fetch upstream
# git checkout master
# git merge upstream/master

windows=${1:-0};
protobufInclude=${2:""};  #-I/c/code/libraries/vcpkg/installed/x64-windows/include -I. messages.proto
baseDir=`pwd`; 
jdeRoot=jde
if [ ! -d json ]; then git clone https://github.com/nlohmann/json.git; fi;
if [ ! -d json ]; then git clone https://github.com/gabime/spdlog.git; fi;
if [ ! -d tws-api ]; then 
	git clone https://github.com/johnduffynh/tws-api.git;
	cd tws-api/source/cppclient/client/
	msbuild.exe TwsSocketClient.vcxproj -p:Configuration=Release -p:Platform=x64 -maxCpuCount
	msbuild.exe TwsSocketClient.vcxproj -p:Configuration=Debug -p:Platform=x64 -maxCpuCount
	cd $baseDir;
fi;


if [ ! -d $jdeRoot ]; then mkdir $jdeRoot; fi;
cd $jdeRoot
if [ $windows -eq 1 ]; then
	git clone https://github.com/Jde-cpp/Windows.git;
else
	git clone https://github.com/Jde-cpp/Linux.git;
fi;
if [ ! -d Framework ]; then 
	git clone https://github.com/Jde-cpp/Framework.git & cd Framework;
else
	cd Framework & git pull;
fi;
cd source/log/server/proto;

cd Framework/source/log/server/proto;
protoc --cpp_out dllexport_decl=JDE_NATIVE_VISIBILITY:. $protobufInclude -I. messages.proto
cd ../../..
mkdir .obj;
if [ $windows -eq 1 ]; then
	msbuild.exe Framework.vcxproj -p:Configuration=Debug -p:Platform=x64 -maxCpuCount
	msbuild.exe Framework.vcxproj -p:Configuration=Release -p:Platform=x64 -maxCpuCount
else
	./buildc.sh asan
	./buildc.sh release
	./buildc.sh RelWithDebInfo
fi;


cd $baseDir/$jdeRoot
if [ ! -d MarketLibrary ]; then 
	git clone https://github.com/Jde-cpp/MarketLibrary.git & cd MarketLibrary;
else
	cd MarketLibrary & git pull;
fi;
cd source/types/proto;
protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. requests.proto;
protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. results.proto;
protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. ib.proto
cd ../..
function copyTwsLib 
{
	if [ ! -d .bin ]; then 
		mkdir .bin;
		cd .bin;
		if [ ! -d Debug ]; then mkdir Debug; fi;
		if [ ! -d Release ]; then mkdir Release; fi;
		cd ..;
	fi;
	#cp $baseDir/tws-api/source/cppclient/client/.bin/Debug/TwsSocketClient.dll .bin/Debug;
	
	#cp $baseDir/tws-api/source/cppclient/client/.bin/Release/TwsSocketClient.dll .bin/Release;
	cp $baseDir/tws-api/source/cppclient/client/.bin/Release/TwsSocketClient.lib .bin/Release;
	cp $baseDir/$jdeRoot/Framework/source/.bin/Debug/Jde.lib .bin/Debug;
	cp $baseDir/$jdeRoot/Framework/source/.bin/Release/Jde.lib .bin/Release;
}
if [ ! -f "Markets.vcxproj.user" ]; then
    cp Markets.vcxproj._user Markets.vcxproj.user
fi;
if [ $windows -eq 1 ]; then
	if [ ! -d .bin ]; then 
		mkdir .bin;
		cd .bin;
		if [ ! -d Debug ]; then mkdir Debug; fi;
		if [ ! -d Release ]; then mkdir Release; fi;
		cd ..;
	fi;
	#cp $baseDir/tws-api/source/cppclient/client/.bin/Debug/TwsSocketClient.dll .bin/Debug;
	cp $baseDir/tws-api/source/cppclient/client/.bin/Debug/TwsSocketClient.lib .bin/Debug;
	#cp $baseDir/tws-api/source/cppclient/client/.bin/Release/TwsSocketClient.dll .bin/Release;
	cp $baseDir/tws-api/source/cppclient/client/.bin/Release/TwsSocketClient.lib .bin/Release;
	cp $baseDir/$jdeRoot/Framework/source/.bin/Debug/Jde.lib .bin/Debug;
	cp $baseDir/$jdeRoot/Framework/source/.bin/Release/Jde.lib .bin/Release;

	msbuild.exe Markets.vcxproj -p:Configuration=Debug -p:Platform=x64 -maxCpuCount
	msbuild.exe Markets.vcxproj -p:Configuration=Release -p:Platform=x64 -maxCpuCount
else
	./buildc.sh asan
	./buildc.sh release
	./buildc.sh RelWithDebInfo
fi;
#########TwsWebSocket#########
cd $baseDir/$jdeRoot;
if [ ! -d TwsWebSocket ]; then 
	git clone https://github.com/Jde-cpp/TwsWebSocket.git; cd TwsWebSocket;
else
	cd TwsWebSocket; git pull;
fi;
cd source/;

if [ $windows -eq 1 ]; then
	if [ ! -f "TwsWebSocket.vcxproj.user" ]; then
		cp TwsWebSocket.vcxproj._user TwsWebSocket.vcxproj.user
	fi;
	copyTwsLib;
	cp $baseDir/$jdeRoot/MarketLibrary/source/.bin/Debug/Jde.Markets.lib .bin/Debug;
	cp $baseDir/$jdeRoot/MarketLibrary/source/.bin/Release/Jde.Markets.lib .bin/Release;

	msbuild.exe TwsWebSocket.vcxproj -p:Configuration=Debug -p:Platform=x64 -maxCpuCount
	msbuild.exe TwsWebSocket.vcxproj -p:Configuration=Release -p:Platform=x64 -maxCpuCount
else
	./buildc.sh asan
	./buildc.sh release
	./buildc.sh RelWithDebInfo
fi;