clean=${1:-0};
shouldFetch=${2:-1};
buildPrivate=${3:-0};
baseDir=`pwd`; jdeRoot=jde;

t=$(readlink -f "${BASH_SOURCE[0]}"); scriptName=$(basename "$t"); unset t;
echo $scriptName clean=$clean shouldFetch=$shouldFetch buildPrivate=$buildPrivate
function myFetch
{
	pushd `pwd`> /dev/null;
	dir=$1;
	if test ! -d jde; then mkdir jde; fi; cd jde;
	if test ! -d $dir; then git clone https://github.com/Jde-cpp/$dir.git; else cd $dir; if (( $shouldFetch == 1 )); then git pull; cd ..; fi; fi;
	popd> /dev/null;
}

myFetch Framework
if [[ -z $sourceBuild ]]; then source jde/Framework/source-build.sh; fi;
if [ ! windows ]; then set disable-completion on; fi;

myFetch MarketLibrary
./jde/MarketLibrary/market-build.sh $clean $shouldFetch; if [ $? -ne 0 ]; then echo market-build.sh failed - $?; exit 1; fi;
includeDir=$REPO_DIR/jde/Public/jde;
cd $includeDir/Blockly/types/proto;
blocklyProtoDir=`pwd`;
if [ $clean -eq 1 ]; then
    rm *.pb.h
fi;
if [ ! -f blockly.pb.h ]; then
	findProtoc; protoc --cpp_out dllexport_decl=JDE_BLOCKLY:. blockly.proto;
fi;

fetchBuild Ssl;
fetchBuild Google;
if test windows; then fetchBuild Odbc 0 Jde.DB.Odbc.dll; fi;

if [ $buildPrivate -eq 1 ]; then
    fetchDefault Blockly;
    if [ ! -d types/proto ]; then cd types;mkdir proto; cd ..; fi;
    if [ -f $blocklyProtoDir/blockly.pb.cc ]; then mv $blocklyProtoDir/blockly.pb.cc ./types/proto/blockly.pb.cc; fi;
    build Blockly;
    build Blockly.Executor;
    fetchDefault Private;
    cd markets/edgar;
    build Edgar 0 Jde.Markets.Edgar.dll
fi;

fetchDefault TwsWebSocket;
build TwsWebSocket 0 TwsWebSocket.exe;



if [ ! windows ]; then set disable-completion off; fi;


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
#b2 variant=debug link=shared threading=multi runtime-link=shared address-model=64 --with-coroutine
#b2 variant=release link=shared threading=multi runtime-link=shared address-model=64 --with-coroutine
# git clone https://github.com/johnduffynh/tws-api.git
# git remote add upstream https://github.com/InteractiveBrokers/tws-api.git
# git fetch upstream
# git checkout master
