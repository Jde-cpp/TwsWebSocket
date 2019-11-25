CCACHE_EXISTS := $(shell ccache -V)
ifdef CCACHE_EXISTS
    CXX := ccache g++-8
endif

DEBUG ?= 1
INCLUDE = -I../framework -I/home/duffyj/code/libraries/tws-api/source/cppclient/client
CXXFLAGS_BASE = -c -g -pthread -fPIC -std=c++17 -Wall -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-switch $(INCLUDE) 

BIN_DIR_BASE:=../../bin/
OUT_DIR_BASE := .obj/
LIBS_BASE = -lMy -lMarkets -lboost_system -lIb -lprotobuf -lstdc++fs

ifeq ($(DEBUG), 1)
	OUT_DIR=$(OUT_DIR_BASE)debug
	CXXFLAGS= $(CXXFLAGS_BASE) -O0 -I$(OUT_DIR) -fsanitize=address -fno-omit-frame-pointer
	BIN_DIR=$(BIN_DIR_BASE)debug
	LIBS = -lasan $(LIBS_BASE)
else
	OUT_DIR=$(OUT_DIR_BASE)release
	CXXFLAGS= $(CXXFLAGS_BASE) -DNDEBUG -O3 -I$(OUT_DIR)
	BIN_DIR=$(BIN_DIR_BASE)release
	LIBS = $(LIBS_BASE) -ldl
endif

OUTPUT = $(BIN_DIR)/TwsWebSocket

LIBFLAGS = -pthread -L$(BIN_DIR)  -Wl,-z,origin -Wl,-rpath='$$ORIGIN' 

all: $(OUTPUT)
	$(NOECHO) $(NOOP)

OBJECTS = $(OUT_DIR)/main.o $(OUT_DIR)/WebSocket.o $(OUT_DIR)/WebSocketReceive.o $(OUT_DIR)/WebSocketSend.o $(OUT_DIR)/WrapperWeb.o $(OUT_DIR)/Flex.o 

$(OUTPUT): $(OUT_DIR)/stdafx.h.gch $(OBJECTS)
	$(CXX) $(LIBFLAGS) $(OBJECTS) -o$(OUTPUT) $(LIBS)

$(OUT_DIR)/stdafx.h.gch: pc.h
	$(CXX) $(CXXFLAGS) ./pc.h -o $(OUT_DIR)/stdafx.h.gch -I../../Framework/source -I../../MarketLibrary/source -I/home/duffyj/code/libraries/spdlog/include -I$$BOOST_ROOT -I/home/duffyj/code/libraries/json/include -I/home/duffyj/code/libraries/eigen

$(OUT_DIR)/main.o: main.cpp pc.h
	$(CXX) $(CXXFLAGS) $< -o$@  $(INCLUDE)

$(OUT_DIR)/Historian.o: ./Historian.cpp ./Historian.h pc.h
	$(CXX) $(CXXFLAGS) ./$< -o$@ $(INCLUDE) -I/home/duffyj/code/libraries/eigen 

$(OUT_DIR)/WebSocketReceive.o: ./WebSocketReceive.cpp ./WebSocket.h pc.h
	$(CXX) $(CXXFLAGS) ./$< -o$@ $(INCLUDE)

$(OUT_DIR)/WebSocketSend.o: ./WebSocketSend.cpp ./WebSocket.h pc.h
	$(CXX) $(CXXFLAGS) -Wno-maybe-uninitialized ./$< -o$@ $(INCLUDE)

$(OUT_DIR)/%.o: ./%.cpp %.h pc.h
	$(CXX) $(CXXFLAGS) ./$< -o$@ $(INCLUDE)

clean:
	rm -rf -d $(OUT_DIR)/*.*
	rm -f $(OUTPUT)
