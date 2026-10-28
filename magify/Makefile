CC ?= gcc
WINDRES ?= windres

CFLAGS = -std=c11 -O3 -Wall -Wextra -Werror -municode -mwindows -Iinclude -Ithird_party
LDFLAGS = -static -static-libgcc -municode -mwindows -lgdi32 -luser32 -lshell32 -lole32 -luuid -lwindowscodecs -lshlwapi -ldwmapi -lcomctl32 -lcomdlg32 -ladvapi32 -lgdiplus

SRCS = src/main.c src/ipc.c src/registry.c src/com_extension.c src/ui_overlay.c \
       src/ops_common.c src/ops_image.c src/ops_pdf.c

OBJS = build/main.o build/ipc.o build/registry.o build/com_extension.o build/ui_overlay.o \
       build/ops_common.o build/ops_image.o build/ops_pdf.o

RES = build/magify_res.o
TARGET = build/magify.exe

all: $(TARGET)

$(TARGET): $(OBJS) $(RES)
	$(CC) $(OBJS) $(RES) $(LDFLAGS) -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): assets/magify.rc assets/magify.manifest assets/magify.ico | build
	$(WINDRES) -I. -Iassets assets/magify.rc -O coff -o $(RES)

build:
	-cmd /c if not exist build mkdir build 2>nul

clean:
	-rm -f build/*.o build/*.obj build/*.res build/magify_res.o build/magify.exe build/test_core.exe 2>/dev/null || cmd /c "del /q /f build\*.o build\*.obj build\*.res build\magify_res.o build\magify.exe build\test_core.exe 2>nul"

install: $(TARGET)
	$(TARGET) --install

uninstall: $(TARGET)
	$(TARGET) --uninstall

build/test_core.exe: tests/test_core.c src/com_extension.c src/registry.c src/ipc.c src/ops_common.c src/ui_overlay.c src/ops_image.c src/ops_pdf.c $(TARGET)
	$(CC) -std=c11 -O3 -Wall -Wextra -Werror -static -static-libgcc -municode -Iinclude -Ithird_party tests/test_core.c src/com_extension.c src/registry.c src/ipc.c src/ops_common.c src/ui_overlay.c src/ops_image.c src/ops_pdf.c -municode -lgdi32 -luser32 -lshell32 -lole32 -luuid -lwindowscodecs -lshlwapi -ldwmapi -lcomctl32 -lcomdlg32 -ladvapi32 -lgdiplus -o $@

test: build/test_core.exe
	build/test_core.exe

.PHONY: all clean install uninstall test
