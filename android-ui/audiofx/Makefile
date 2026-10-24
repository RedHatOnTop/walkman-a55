# libwalkmanfx.so for the NW-A55 Android image (/system/lib/soundfx) and the
# host checks that load it.
#
#   make -C android-ui/audiofx test      # host build + tst_walkmanfx
#   make -C android-ui/audiofx android   # build/audiofx/armeabi-v7a/libwalkmanfx.so

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
OUT := $(HERE)../build/audiofx
NDK ?= $(HOME)/Android/sdk/ndk/21.4.7075529
# The image is Android 5.1, API 22.
NDK_CXX := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi22-clang++

SOURCES := $(HERE)effect.cpp $(HERE)walkmanfx.cpp
HEADERS := $(wildcard $(HERE)*.h)
FLAGS := -std=c++14 -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -fno-exceptions -fno-rtti -isystem $(HERE)aosp

.PHONY: all host android test
all: test android
host: $(OUT)/host/libwalkmanfx.so $(OUT)/host/tst_walkmanfx
android: $(OUT)/armeabi-v7a/libwalkmanfx.so

$(OUT)/host/libwalkmanfx.so: $(SOURCES) $(HEADERS)
	mkdir -p $(@D)
	g++ $(FLAGS) -shared $(SOURCES) -o $@ -lpthread

$(OUT)/host/tst_walkmanfx: $(HERE)tests/tst_walkmanfx.cpp $(HERE)walkmanfx.cpp $(HEADERS)
	mkdir -p $(@D)
	g++ $(FLAGS) $(HERE)tests/tst_walkmanfx.cpp $(HERE)walkmanfx.cpp -o $@ -ldl -lm

test: host
	$(OUT)/host/tst_walkmanfx $(OUT)/host/libwalkmanfx.so

# mediaserver dlopens it: no C++ runtime of its own, only libc and libm. Without a soname
# gold names the version definition after the output path, the build directory.
$(OUT)/armeabi-v7a/libwalkmanfx.so: $(SOURCES) $(HEADERS) Makefile
	mkdir -p $(@D)
	$(NDK_CXX) $(FLAGS) -shared $(SOURCES) -o $@ -nostdlib++ -Wl,-soname,libwalkmanfx.so \
	    -Wl,--no-undefined -Wl,-z,noexecstack -lm
	$(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-unneeded $@
