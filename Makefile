CC=/usr/bin/clang++

CPPFILES=$(wildcard src/*.cpp)
HPPFILES=$(wildcard src/*.hpp)
LIBFILES=$(wildcard src/discord/*.cpp)
CFLAGS=-Llib/ -l:discord_game_sdk.so -lpthread -lX11 -march=znver2 -mtune=znver2 -O3

build/rpcpp: $(CPPFILES) $(HPPFILES)
	mkdir -p build
	$(CC) $(CPPFILES) $(LIBFILES) $(CFLAGS) -o $@

clean:
	rm -rf tmp build

install: build/rpcpp
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${PREFIX}/lib
	cp -f build/rpcpp ${DESTDIR}${PREFIX}/bin
# cp to /lib64 for fedora
	cp -f lib/discord_game_sdk.so ${DESTDIR}${PREFIX}/lib64
	chmod 755 ${DESTDIR}${PREFIX}/bin/rpcpp

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/rpcpp

.PHONY: clean install uninstall