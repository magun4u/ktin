CXX := x86_64-w64-mingw32-g++
WINDRES := x86_64-w64-mingw32-windres

TARGET := ktin.exe
INSTALL_DIR ?= /mnt/c/ktin
RESOBJ := Resource.o
SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)
KTIN_BUILD_ID := $(shell sed -n 's/^#define KTIN_APP_VER_BUILD[[:space:]]\+\([0-9]\+\).*/\1/p' app_version.h)
BUILD_STAMP := .ktin_build_$(KTIN_BUILD_ID).stamp

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX \
	-finput-charset=UTF-8 -fexec-charset=UTF-8
DEPFLAGS := -MMD -MP

LDFLAGS := -municode -mwindows -static -static-libgcc -static-libstdc++
LDLIBS := -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32 -luxtheme \
	-lwinmm -ldwmapi -lversion -lole32 -loleaut32

RCFLAGS := --codepage=65001
ifneq ($(wildcard MudDunggeunmo-Regular.ttf),)
RCFLAGS += -DHAVE_EMBEDDED_FONT
endif

.PHONY: all install install-wsl clean clean-objs clean-bin clean-msvc nores

all: $(TARGET)

$(TARGET): $(OBJS) $(RESOBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

nores: $(OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $^ $(LDLIBS)

install: all
	mkdir -p "$(INSTALL_DIR)"
	cp -f "$(TARGET)" "$(INSTALL_DIR)/$(TARGET)"

install-wsl: install

$(BUILD_STAMP): app_version.h Makefile
	$(RM) $(OBJS) $(DEPS) $(RESOBJ) .ktin_build_*.stamp
	touch $@

$(OBJS) $(RESOBJ): $(BUILD_STAMP) app_version.h

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(RESOBJ): Resource.rc resource.h app_version.h ktin.ico
	$(WINDRES) $(RCFLAGS) -i Resource.rc -o $@

clean: clean-objs clean-bin clean-msvc

clean-objs:
	$(RM) $(OBJS) $(DEPS) $(RESOBJ) .ktin_build_*.stamp
	find . \( -name "*.o" -o -name "*.obj" -o -name "*.res" -o -name "*.aps" \) -type f -delete

clean-bin:
	$(RM) $(TARGET)

clean-msvc:
	find . \( -name "*.pdb" -o -name "*.ilk" -o -name "*.idb" -o -name "*.ipdb" -o -name "*.iobj" -o -name "*.tlog" -o -name "*.lastbuildstate" \) -type f -delete

-include $(DEPS)
