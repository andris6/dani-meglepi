#!/usr/bin/python3

import asyncio
import websockets

def logo():
    print("-----------------------")
    print(" dani-meglepi-controlb ")
    print("-----------------------\n")

async def get_user_input():
    return await asyncio.get_event_loop().run_in_executor(None, input, "$ ")

async def handler(websocket):
    print("[Client connected!]")
    try:
        while True:
            cmd = await get_user_input()
            if not cmd: continue
            
            await websocket.send(cmd)
            
            response = await websocket.recv()
            print(response)
            
    except websockets.ConnectionClosed:
        print("[Client disconnected.]")

async def main():
    async with websockets.serve(handler, "127.0.0.1", 1928):
        print("[Server started on ws://127.0.0.1:1928]")
        await asyncio.Future()

if __name__ == "__main__":
    logo()
    asyncio.run(main())

