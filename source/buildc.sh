#!/bin/bash
type=${1:-asan}
clean=${2:-0}
all=${3:-1}

if [ $all -eq 1 ]; then
	../../Framework/cmake/buildc.sh ../../Framework/source $type $clean || exit 1;
	../../Framework/cmake/buildc.sh ../../MarketLibrary/source $type $clean || exit 1;
fi
output=.obj/$type
if [ ! -d $output ]; then mkdir $output; fi;
cd $output

if [ $clean -eq 1 ]; then
	rm CMakeCache.txt;
	cmake -DCMAKE_BUILD_TYPE=$type  ../.. > /dev/null;
	make clean;
fi
make -j7
cd - > /dev/null
exit $?
