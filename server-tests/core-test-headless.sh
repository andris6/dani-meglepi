python3 -c "
import threading, asyncio, time, sys

import unittest.mock as mock
sys.modules['tkinter'] = mock.MagicMock()
sys.modules['tkinter.ttk'] = mock.MagicMock()
sys.modules['tkinter.messagebox'] = mock.MagicMock()

import importlib.util, types
spec = importlib.util.spec_from_file_location('srv', '../dani-meglepi-server.py')
mod = importlib.util.module_from_spec(spec)

import tkinter as tk
tk.Tk = mock.MagicMock()

spec.loader.exec_module(mod)

async def test():
  cm = mod.ConnectionManager()
  assert cm.client_count() == 0
  await cm.broadcast('show')
  assert cm.current_state == 'show'
  await cm.broadcast('clear')
  assert cm.current_state == 'idle'
  print('ConnectionManager: OK')

asyncio.run(test())
print('Importálás és logika: OK')
"
