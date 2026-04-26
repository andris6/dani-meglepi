#!/usr/bin/bash

if [[ "$1" = "uninstall" ]]; then
  sudo rm /usr/bin/dani-meglepi-server
fi

cp ../dani-meglepi-server.py

pyinstaller --onefile --windowed --noconsole --name dani-meglepi-server ./dani-meglepi-server.py

sudo cp ./dist/dani-meglepi-server /usr/bin/dani-meglepi-server
