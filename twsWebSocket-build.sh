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

myFetch MarketLibrary market-build.sh
cd jde/MarketLibrary;
./market-build.sh $clean $shouldFetch 1; if [ $? -ne 0 ]; then echo market-build.sh failed - $?; exit 1; fi;
echo market-build complete
echo scriptDir=$scriptDir;
includeDir=$scriptDir/jde/Public/jde;
cd $includeDir/blockly/types/proto;
blocklyProtoDir=`pwd`;
if [ $clean -eq 1 ]; then
    rm *.pb.h > /dev/null;
fi;
if [ ! -f blockly.pb.h ]; then
	findProtoc; protoc --cpp_out dllexport_decl=JDE_BLOCKLY:. blockly.proto;
fi;

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
    build Blockly;
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
./setup.sh $clean $shouldFetch $buildPrivate;
if ! $(findExecutable devenv.exe '/c/Program\ Files\ \(X86\)/Microsoft\ Visual\ Studio/2019/BuildTools/Common7/IDE' 0 ); then findExecutable devenv.exe '/c/Program\ Files\ \(X86\)/Microsoft\ Visual\ Studio/2019/Enterprise/Common7/IDE'; fi;
cd $scriptDir
devenv jde/TwsWebSocket/setup/Setup.vdproj;



