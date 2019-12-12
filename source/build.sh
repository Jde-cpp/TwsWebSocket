#!/bin/bash
debug=${1:-1}
all=${2:-1}
clean=${3:-0}

if [ $all -eq 1 ]; then
	../../Framework/source/build.sh $debug $clean || exit 1;
	../../MarketLibrary/source/build.sh $debug $clean || exit 1;
fi
if [ $clean -eq 1 ]; then
	make clean DEBUG=$debug
	if [ $debug -eq 1 ]; then
		ccache g++-8 -c -g -pthread -fPIC -std=c++17 -Wall -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-switch -I../../Framework/source -I../../MarketLibrary/source -I/home/duffyj/code/libraries/tws-api/source/cppclient/client  -O0 -I.debug -fsanitize=address -fno-omit-frame-pointer ./pc.h -o .obj/debug2/stdafx.h.gch -I/home/duffyj/code/libraries/spdlog/include -I$BOOST_ROOT -I/home/duffyj/code/libraries/json/include
	else
		ccache g++-8 -c -g -pthread -fPIC -std=c++17 -Wall -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-switch -I../../Framework/source -I../../MarketLibrary/source -I/home/duffyj/code/libraries/tws-api/source/cppclient/client  -DNDEBUG -O3 -I.release ./pc.h -o .obj/release2/stdafx.h.gch -I/home/duffyj/code/libraries/spdlog/include -I$BOOST_ROOT -I/home/duffyj/code/libraries/json/include
	fi
	if [ $? -eq 1 ]; then
		exit 1
	fi
fi
make -j7 DEBUG=$debug
if [ $? -eq 1 ]; then
	exit 1
fi
