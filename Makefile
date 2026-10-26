MAKE ?= make

all:
	$(MAKE) -C magify all
	mkdir -p build
	cp -f magify/build/magify.exe build/magify.exe

clean:
	$(MAKE) -C magify clean
	rm -rf build

test:
	$(MAKE) -C magify test
	mkdir -p build
	cp -f magify/build/test_core.exe build/test_core.exe

.PHONY: all clean test
