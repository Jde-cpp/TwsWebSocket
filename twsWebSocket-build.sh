#!/bin/bash
##cls;source jde/Framework/common.sh;cd jde/TwsWebSocket; rm twsWebSocket-build.sh; mv ../../twsWebSocket-build.sh .;cd ../..;mklink twsWebSocket-build.sh jde/TwsWebSocket
clean=${1:-0};
shouldFetch=${2:-1};
buildPrivate=${3:-1};
buildWeb=${4:-1};

t=$(readlink -f "${BASH_SOURCE[0]}"); scriptName=$(basename "$t"); unset t;
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
echo $scriptName clean=$clean shouldFetch=$shouldFetch buildPrivate=$buildPrivate
function myFetch
{
	pushd `pwd`> /dev/null;
	dir=$1;
	script=$2;
	if test ! -d jde; then mkdir jde; fi; cd jde;
	JDE_BASH=`pwd`;
	if test ! -d $dir; then
		git clone https://github.com/Jde-cpp/$dir.git;
		cd $dir;
		chmod 777 $script;
	else
		cd $dir;
		if (( $shouldFetch == 1 )); then git pull; cd ..; fi;
	fi;
	popd> /dev/null;
}

myFetch Framework framework-build.sh
if [[ -z $sourceBuild ]]; then source jde/Framework/source-build.sh; fi;
if ! windows; then set disable-completion on; fi;
source $JDE_BASH/Framework/common.sh;
$JDE_BASH/Framework/framework-build.sh $clean $shouldFetch $buildBoost; if [ $? -ne 0 ]; then echo framework-build.sh failed - $?; exit 1; fi;
######################################################
echo framework-build.sh complete
# `pwd`> /dev/null;
cd $JDE_BASH/Public/jde/blockly/types/proto;
blocklyProtoDir=`pwd`;
echo blocklyProtoDir=$blocklyProtoDir
if [ $clean -eq 1 ]; then
    rm *.pb.h > /dev/null;
fi;
if [ ! -f blockly.pb.h ]; then
	findProtoc; protoc --cpp_out dllexport_decl=JDE_BLOCKLY:. blockly.proto;
	sed -i -e 's/JDE_BLOCKLY/ΓB/g' blockly.pb.h;sed -i '1s/^/\xef\xbb\xbf/' blockly.pb.h;
	if windows; then
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT CopyDefaultTypeInternal _Copy_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY CopyDefaultTypeInternal _Copy_default_instance_;/' blockly.pb.cc;
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT IdRequestDefaultTypeInternal _IdRequest_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY IdRequestDefaultTypeInternal _IdRequest_default_instance_;/' blockly.pb.cc;
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT FunctionDefaultTypeInternal _Function_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY FunctionDefaultTypeInternal _Function_default_instance_;/' blockly.pb.cc;
	fi;
fi;
marketsProtoDir=$JDE_BASH/Public/jde/markets/types/proto;
cd $marketsProtoDir;
if [ ! -f blocklyResults.pb.h ]; then
	findProtoc; protoc --cpp_out dllexport_decl=JDE_BLOCKLY_EXECUTOR:. blocklyResults.proto;
	sed -i -e 's/JDE_BLOCKLY_EXECUTOR/ΓBE/g' blocklyResults.pb.h;sed -i '1s/^/\xef\xbb\xbf/' blocklyResults.pb.h;
	if windows; then
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT FileEntryDefaultTypeInternal _FileEntry_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY FileEntryDefaultTypeInternal _FileEntry_default_instance_;/' blocklyResults.pb.cc;
	fi;
fi;
if [ ! -f ib.pb.h ]; then
	echo create ib.pb.h;
	findProtoc; protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. ib.proto;
	sed -i -e 's/JDE_MARKETS_EXPORT/ΓM/g' ib.pb.h;sed -i '1s/^/\xef\xbb\xbf/' ib.pb.h;
fi;
if [ ! -f results.pb.h ]; then
	echo create results.pb.h;
	findProtoc; protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. results.proto;
	sed -i -e 's/JDE_MARKETS_EXPORT/ΓM/g' results.pb.h;sed -i '1s/^/\xef\xbb\xbf/' results.pb.h;
fi;
if [ ! -f requests.pb.h ]; then
	echo create requests.pb.h;
	findProtoc; protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. requests.proto;
	sed -i -e 's/JDE_MARKETS_EXPORT/ΓM/g' requests.pb.h;sed -i '1s/^/\xef\xbb\xbf/' requests.pb.h;
fi;
if [ ! -f watch.pb.h ]; then
	echo create watch.proto;
	findProtoc; protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. watch.proto;
	sed -i -e 's/JDE_MARKETS_EXPORT/ΓM/g' watch.pb.h;sed -i '1s/^/\xef\xbb\xbf/' watch.pb.h;sed -i -e 's/JDE_MARKETS_EXPORT//g' watch.pb.cc;
fi;
if [ ! -f edgar.pb.h ]; then
	echo create edgar.proto;
	findProtoc; protoc --cpp_out dllexport_decl=JDE_MARKETS_EXPORT:. edgar.proto;
	sed -i -e 's/JDE_MARKETS_EXPORT/ΓM/g' edgar.pb.h;sed -i '1s/^/\xef\xbb\xbf/' edgar.pb.h;
fi;
######################################################
fetchDefault MarketLibrary
######################################################
if [ $buildPrivate -eq 1 ]; then
	fetchDefault Blockly;
	if [ ! -d types/proto ]; then cd types;mkdir proto; cd ..; fi;
	cd types/proto
	if [ -f $blocklyProtoDir/blockly.pb.cc ]; then
		#echo mv $blocklyProtoDir/blockly.pb.cc .;
		mv $blocklyProtoDir/blockly.pb.cc .;
		mklink blockly.pb.h $blocklyProtoDir;
	fi;
	if [ ! -f blocklyResults.pb.cc ]; then
		#echo make blockly proto dir `pwd`
		mv $marketsProtoDir/blocklyResults.pb.cc .;
		mklink blocklyResults.pb.h $marketsProtoDir;
		mklink ib.pb.h $marketsProtoDir;  #TODO
	fi;
	cd ../..;
	build Blockly;
fi;

# cd $JDE_BASH/..
# echo `pwd`
# echo myFetch MarketLibrary market-build.sh
# myFetch MarketLibrary market-build.sh
cd $JDE_BASH/MarketLibrary;
$JDE_BASH/MarketLibrary/market-build.sh $clean $shouldFetch 0; if [ $? -ne 0 ]; then echo market-build.sh failed - $?; exit 1; fi;
echo market-build complete

fetchBuild Ssl;
fetchBuild Google;
if windows; then fetchBuild Odbc 0 Jde.DB.Odbc.dll; fi;

if [ $buildPrivate -eq 1 ]; then
	cd $JDE_BASH/Blockly/source;

	mklink ib.pb.h $marketsProtoDir;  #TODO
	build Blockly.Executor;
	fetchDefault Private;
	cd markets/edgar/types;moveToDir proto;
	if [ ! -f edgar.pb.h ]; then mklink edgar.pb.h $marketsProtoDir; fi;
	cd ../..;
	build Edgar 0 Jde.Markets.Edgar.dll
fi;

fetchDefault TwsWebSocket;
build TwsWebSocket 0 TwsWebSocket.exe;

if [ ! windows ]; then set disable-completion off; fi;
if [ $buildWeb -eq 0 ]; then exit 0; fi;
#----------------------------------------------------------------------------------------------------------
cd $scriptDir/jde;
moveToDir web;
fetch TwsWebsite;
cd ..;
echo --------------------------------------------------------------Start Setup--------------------------------------------------------------;
./setup.sh $clean $shouldFetch $buildPrivate; if [ $? -ne 0 ]; then echo setup.sh failed - $?; exit 1; fi;
cd $scriptDir/jde/TwsWebSocket;

if windows; then
	./win-install.sh; if [ $? -ne 0 ]; then echo win-install.sh failed - $?; exit 1; fi;

	echo ---------------------------------------------------------------End Setup---------------------------------------------------------------;
	cd $JDE_BASH/Public/stage/release;
	mklink liblzma.dll $REPO_BASH/xz-5.2.5-windows/bin_x86-64;
	mklink zlib1.dll $REPO_BASH/vcpkg/buildtrees/zlib/x64-windows-rel;
	cd $scriptDir;
	findExecutable devenv.exe '/c/Program\ Files/Microsoft\ Visual\ Studio/2022/BuildTools/Common7/IDE' 0
	findExecutable devenv.exe '/c/Program\ Files/Microsoft\ Visual\ Studio/2022/Enterprise/Common7/IDE'
	echo -------------------------------------------------------------Start Install-------------------------------------------------------------;
	devenv.exe jde/TwsWebSocket/setup/Setup.vdproj //build release;
	echo --------------------------------------------------------------End Install--------------------------------------------------------------;
	if [ ! -f jde/TwsWebSocket/setup/Release/Setup.msi ]; then echo Setup Failed; else echo success; fi;
fi;