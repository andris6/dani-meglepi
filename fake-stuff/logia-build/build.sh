#!/usr/bin/bash

cd ../logia/

x86_64-w64-mingw32-g++ main.cpp -o Logia.exe -lgdi32 -static -static-libgcc -static-libstdc++ -mwindows

