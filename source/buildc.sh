#!/bin/bash
type=${1:-asan}
clean=${2:-0}
all=${3:-1}
export CXX=clang++
if [ $all -eq 1 ]; then
	../../Framework/cmake/buildc.sh ../../Framework/source $type $clean || exit 1;
	../../Framework/cmake/buildc.sh ../../MarketLibrary/source $type $clean || exit 1;
	../../Framework/cmake/buildc.sh ../../Blockly/source $type $clean || exit 1;
	../../Framework/cmake/buildc.sh ../../Ssl/source $type $clean || exit 1;
	../../Framework/cmake/buildc.sh ../../Private/source/markets/edgar $type $clean || exit 1;
	../../Framework/cmake/buildc.sh ../../MySql/source $type $clean || exit 1;
fi

if [ ! -d .obj ]; then mkdir .obj; fi;
cd .obj;
if [ $clean -eq 1 ]; then
	rm -r -f $type
fi;
if [ ! -d $type ]; then mkdir $type; fi;
cd $type

if [ ! -f Makefile ]; then
	cmake -DCMAKE_BUILD_TYPE=$type  ../.. > /dev/null;
	make clean;
fi
make -j7
cd - > /dev/null
exit $?
