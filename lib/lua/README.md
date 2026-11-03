Lua 5.4.7 from https://github.com/lua/lua (tag v5.4.7). One change: LUA_32BITS is 1 in
luaconf.h (32-bit integers and floats; the ESP32-S3 has a float unit). Left out:
the stand-alone interpreter (lua.c, onelua.c), the test files, and the io, os,
package and debug libraries (liolib.c, loslib.c, loadlib.c, ldblib.c, linit.c),
so a script app cannot reach files or the system. License: MIT, see the end of
src/lua.h.
