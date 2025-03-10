CC = g++
CPPFILES = $(wildcard src/*.cpp)
LIBFILES = $(wildcard src/discord/*.cpp)
LDLAGS = -Llib/ -l:discord_game_sdk.so -lpthread
CXXFLAGS = -O3 -march=native -mtune=native

.PHONY: clean build install uninstall

clean:
	rm -rf build

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/rpcpp

build: $(CPPFILES)
	mkdir -p build
	$(CC) $(CXXFLAGS) $(CPPFILES) $(LIBFILES) $(CFLAGS) -o build/rpcpp $(LDLAGS)

install:
	cp -f build/rpcpp $(DESTDIR)$(PREFIX)/bin
	cp -f lib/discord_game_sdk.so $(DESTDIR)$(PREFIX)/lib
	cp -f lib/discord_game_sdk.so $(DESTDIR)$(PREFIX)/lib64
	chmod 755 $(DESTDIR)$(PREFIX)/bin/rpcpp
