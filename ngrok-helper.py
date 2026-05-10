#!/usr/bin/env python3
"""
ngrok-helper.py — dani-meglepi
"""
import sys, signal, time

try:
    import ngrok
except ImportError:
    print("ERROR: pip install ngrok", flush=True)
    sys.exit(1)

AUTHTOKEN = "3CrfFkoxfbzOqvibRPgphslSE5t_6PMdjjxy75gX3Guqi5EYw"
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

def main():
    try:
        ngrok.set_auth_token(AUTHTOKEN)
        listener = ngrok.forward(f"localhost:{port}")
        url = listener.url()
        # URL kiírása stdout-ra (Node.js ezt olvassa)
        print(url, flush=True)
        # Él amíg SIGTERM/SIGINT nem jön
        signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
        while True:
            time.sleep(1)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
