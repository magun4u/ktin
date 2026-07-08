CXX := x86_64-w64-mingw32-g++
WINDRES := x86_64-w64-mingw32-windres

TARGET := ktin.exe
RESOBJ := Resource.o
SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX \
	-finput-charset=UTF-8 -fexec-charset=UTF-8

LDFLAGS := -municode -mwindows -static -static-libgcc -static-libstdc++
LDLIBS := -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32 -luxtheme \
	-lwinmm -ldwmapi -lversion -lole32 -loleaut32

RCFLAGS := --codepage=65001
ifneq ($(wildcard MudDunggeunmo-Regular.ttf),)
RCFLAGS += -DHAVE_EMBEDDED_FONT
endif

.PHONY: all clean nores

all: $(TARGET)

$(TARGET): $(OBJS) $(RESOBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

nores: $(OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RESOBJ): Resource.rc resource.h ktin.ico
	$(WINDRES) $(RCFLAGS) -i Resource.rc -o $@

clean:
	$(RM) $(OBJS) $(RESOBJ) $(TARGET)
