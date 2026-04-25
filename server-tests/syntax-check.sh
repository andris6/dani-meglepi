python3 -c "
import ast
with open('../dani-meglepi-server.py') as f:
  src = f.read()
try:
  ast.parse(src)
  print('Szintaxis rendben van')
except SyntaxError as e:
  print(f'Szintaxis hiba: {e}')
"
