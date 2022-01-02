#!/bin/bash
clean=${1:-0};
shouldFetch=${2:-1};
buildPrivate=${3:-1};
buildWeb=${4:-1};

#baseDir=`pwd`; jdeRoot=jde;

t=$(readlink -f "${BASH_SOURCE[0]}"); scriptName=$(basename "$t"); unset t;
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
echo $scriptName clean=$clean shouldFetch=$shouldFetch buildPrivate=$buildPrivate
function myFetch
{
	pushd `pwd`> /dev/null;
	dir=$1;
	script=$2;
	if test ! -d jde; then mkdir jde; fi; cd jde;
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

$JDE_BASH/Framework/framework-build.sh $clean $shouldFetch $buildBoost; if [ $? -ne 0 ]; then echo framework-build.sh failed - $?; exit 1; fi;
######################################################
pushd `pwd`> /dev/null;
cd $JDE_BASH/Public/jde/blockly/types/proto;
blocklyProtoDir=`pwd`;
if [ $clean -eq 1 ]; then
    rm *.pb.h > /dev/null;
fi;
if [ ! -f blockly.pb.h ]; then
	findProtoc; protoc --cpp_out dllexport_decl=JDE_BLOCKLY:. blockly.proto;
	if windows; then
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT CopyDefaultTypeInternal _Copy_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY CopyDefaultTypeInternal _Copy_default_instance_;/' blockly.pb.cc;
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT IdRequestDefaultTypeInternal _IdRequest_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY IdRequestDefaultTypeInternal _IdRequest_default_instance_;/' blockly.pb.cc;
		sed -i 's/PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT FunctionDefaultTypeInternal _Function_default_instance_;/PROTOBUF_ATTRIBUTE_NO_DESTROY FunctionDefaultTypeInternal _Function_default_instance_;/' blockly.pb.cc;
	fi;
fi;
popd> /dev/null;
######################################################
if [ $buildPrivate -eq 1 ]; then
	fetchDefault Blockly;
	if [ ! -d types/proto ]; then cd types;mkdir proto; cd ..; fi;
	cd types/proto
	if [ -f $blocklyProtoDir/blockly.pb.cc ]; then
		mv $blocklyProtoDir/blockly.pb.cc .;
		mkklink blockly.pb.h $blocklyProtoDir;
	fi;
	cd ../..;
    build Blockly;
fi;
cd $JDE_BASH/..
echo `pwd`
echo myFetch MarketLibrary market-build.sh
myFetch MarketLibrary market-build.sh
cd jde/MarketLibrary;
$JDE_BASH/MarketLibrary/market-build.sh $clean $shouldFetch 0; if [ $? -ne 0 ]; then echo market-build.sh failed - $?; exit 1; fi;
echo market-build complete

fetchBuild Ssl;
fetchBuild Google;
if windows; then fetchBuild Odbc 0 Jde.DB.Odbc.dll; fi;

if [ $buildPrivate -eq 1 ]; then
	fetchDefault Blockly;
	if [ ! -d types/proto ]; then cd types;mkdir proto; cd ..; fi;
	if [ -f $blocklyProtoDir/blockly.pb.cc ]; then
		mv $blocklyProtoDir/blockly.pb.cc ./types/proto/blockly.pb.cc;
		ln -s $blocklyProtoDir/blockly.pb.h ./types/proto/blockly.pb.h;
	fi;
    #build Blockly;
    build Blockly.Executor;
    fetchDefault Private;
    cd markets/edgar;
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
./win-install.sh if [ $? -ne 0 ]; then echo win-install.sh failed - $?; exit 1; fi;
cd $scriptDir;
echo ---------------------------------------------------------------End Setup---------------------------------------------------------------;
#if ! $(findExecutable devenv.exe '/c/Program\ Files\ \(X86\)/Microsoft\ Visual\ Studio/2019/BuildTools/Common7/IDE' 0 ); then findExecutable devenv.exe '/c/Program\ Files\ \(X86\)/Microsoft\ Visual\ Studio/2019/Enterprise/Common7/IDE'; fi;
findExecutable devenv.exe '/c/Program\ Files/Microsoft\ Visual\ Studio/2022/BuildTools/Common7/IDE' 0
findExecutable devenv.exe '/c/Program\ Files/Microsoft\ Visual\ Studio/2022/Enterprise/Common7/IDE'

echo -------------------------------------------------------------Start Install-------------------------------------------------------------;
devenv.exe jde/TwsWebSocket/setup/Setup.vdproj //build release;
echo --------------------------------------------------------------End Install--------------------------------------------------------------;
if [ ! -f jde/TwsWebSocket/setup/Release/Setup.msi ]; then echo Setup Failed; else echo success; fi;
