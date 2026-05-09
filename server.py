#!/usr/bin/env python3
"""
dani-meglepi-server
Orchesztrálja a dani-meglepi klienseket.
Tkinter GUI + FastAPI WebSocket szerver + Ngrok alagút.
"""

import asyncio
import json
import subprocess
import threading
import tkinter as tk
from tkinter import ttk, messagebox
from datetime import datetime, timedelta
from typing import Optional
import sys
import os
import time

# ── Függőség-ellenőrzés ──────────────────────────────────────────────────────
try:
    import uvicorn
    from fastapi import FastAPI, WebSocket, WebSocketDisconnect
    from fastapi.responses import JSONResponse
except ImportError:
    print("Hiányzó csomagok! Futtasd: pip install fastapi uvicorn")
    sys.exit(1)

# ── Konfiguráció ─────────────────────────────────────────────────────────────
NGROK_AUTHTOKEN = "3CrfFkoxfbzOqvibRPgphslSE5t_6PMdjjxy75gX3Guqi5EYw"
NGROK_DOMAIN    = "turbulent-renter-liking.ngrok-free.app"
SERVER_HOST     = "0.0.0.0"
SERVER_PORT     = 8765

# ── WebSocket kapcsolatkezelő ────────────────────────────────────────────────
class ConnectionManager:
    def __init__(self):
        self.active: dict[str, WebSocket] = {}
        self.lock = asyncio.Lock()
        self.current_state = "idle"
        self.on_change: Optional[callable] = None

    async def connect(self, ws: WebSocket, client_id: str):
        await ws.accept()
        async with self.lock:
            self.active[client_id] = ws
        self._notify()
        if self.current_state != "idle":
            try:
                await ws.send_text(json.dumps({"cmd": self.current_state}))
            except Exception:
                pass

    async def disconnect(self, client_id: str):
        async with self.lock:
            self.active.pop(client_id, None)
        self._notify()

    async def broadcast(self, cmd: str):
        self.current_state = cmd if cmd != "clear" else "idle"
        msg = json.dumps({"cmd": cmd})
        dead = []
        async with self.lock:
            for cid, ws in self.active.items():
                try:
                    await ws.send_text(msg)
                except Exception:
                    dead.append(cid)
            for cid in dead:
                self.active.pop(cid, None)
        self._notify()

    def client_count(self) -> int:
        return len(self.active)

    def _notify(self):
        if self.on_change:
            self.on_change()

manager = ConnectionManager()

# ── FastAPI ──────────────────────────────────────────────────────────────────
app = FastAPI(title="dani-meglepi-server")

@app.websocket("/ws/{client_id}")
async def websocket_endpoint(ws: WebSocket, client_id: str):
    await manager.connect(ws, client_id)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        await manager.disconnect(client_id)

@app.get("/status")
async def status():
    return JSONResponse({
        "clients": manager.client_count(),
        "state": manager.current_state
    })

# ── Broadcast (szál-biztos) ──────────────────────────────────────────────────
def broadcast_sync(cmd: str):
    if _loop:
        asyncio.run_coroutine_threadsafe(manager.broadcast(cmd), _loop)

# ── Ngrok indítása ───────────────────────────────────────────────────────────
def find_ngrok() -> Optional[str]:
    """Megkeresi az ngrok binárist több lehetséges helyen."""
    candidates = [
        "ngrok",
        os.path.expanduser("~/ngrok"),
        os.path.expanduser("~/.local/bin/ngrok"),
        "/usr/local/bin/ngrok",
        "/usr/bin/ngrok",
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "ngrok"),
    ]
    for c in candidates:
        try:
            result = subprocess.run([c, "version"],
                capture_output=True, timeout=3)
            if result.returncode == 0:
                return c
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None

def start_ngrok(log_fn) -> Optional[subprocess.Popen]:
    ngrok_bin = find_ngrok()
    if not ngrok_bin:
        log_fn("HIBA: 'ngrok' nem talalhato! Telepitsd: https://ngrok.com/download")
        return None

    log_fn(f"Ngrok: {ngrok_bin}")

    # Authtoken konfigurálása (egyszer kell, de nem árt minden alkalommal)
    try:
        r = subprocess.run(
            [ngrok_bin, "config", "add-authtoken", NGROK_AUTHTOKEN],
            capture_output=True, timeout=10)
        if r.returncode == 0:
            log_fn("Ngrok authtoken: OK")
        else:
            log_fn(f"Ngrok authtoken hiba: {r.stderr.decode(errors='replace')}")
    except Exception as e:
        log_fn(f"Ngrok authtoken kivetel: {e}")

    # Ngrok indítása
    try:
        proc = subprocess.Popen(
            [
                ngrok_bin, "http",
                f"--domain={NGROK_DOMAIN}",
                str(SERVER_PORT),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        # Rövid várakozás hogy felépüljön a tunnel
        time.sleep(2)
        if proc.poll() is not None:
            # Már kilépett — hiba
            err = proc.stderr.read().decode(errors='replace')
            log_fn(f"Ngrok azonnal kilépett! Hiba: {err}")
            return None
        log_fn(f"Ngrok fut (PID {proc.pid})")
        log_fn(f"URL: wss://{NGROK_DOMAIN}/ws/<id>")
        return proc
    except Exception as e:
        log_fn(f"Ngrok inditasi hiba: {e}")
        return None

# ── Uvicorn szerver ──────────────────────────────────────────────────────────
_loop: Optional[asyncio.AbstractEventLoop] = None

def run_server():
    global _loop
    _loop = asyncio.new_event_loop()
    asyncio.set_event_loop(_loop)
    config = uvicorn.Config(
        app,
        host=SERVER_HOST,
        port=SERVER_PORT,
        loop="none",
        log_level="warning"
    )
    server = uvicorn.Server(config)
    _loop.run_until_complete(server.serve())

# ── Tkinter GUI ──────────────────────────────────────────────────────────────
class App(tk.Tk):
    PAD = dict(padx=12, pady=6)

    def __init__(self):
        super().__init__()
        self.ngrok_proc: Optional[subprocess.Popen] = None
        self.title("dani-meglepi vezérló")
        self.resizable(False, False)
        self._schedule_job: Optional[str] = None
        self._schedule_target = None

        manager.on_change = self._refresh_status
        self._build_ui()
        self._refresh_status()
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        # Szerver + ngrok indítása az UI felépítése után
        self.after(200, self._start_services)

    def _start_services(self):
        """Uvicorn és ngrok indítása — az UI már látható."""
        # Uvicorn háttérszálban
        server_thread = threading.Thread(target=run_server, daemon=True)
        server_thread.start()
        self._log("WebSocket szerver elindult: ws://0.0.0.0:%d" % SERVER_PORT)

        # Rövid várakozás hogy az uvicorn elinduljon
        self.after(1000, self._start_ngrok)

    def _start_ngrok(self):
        self._log("Ngrok inditasa...")
        def do_ngrok():
            proc = start_ngrok(lambda msg: self.after(0, lambda m=msg: self._log(m)))
            self.ngrok_proc = proc
            if proc:
                self.after(0, lambda: self._lbl_ngrok.config(
                    text=f"Ngrok: fut (PID {proc.pid})", fg="green"))
            else:
                self.after(0, lambda: self._lbl_ngrok.config(
                    text="Ngrok: HIBA! (lasd naplo)", fg="red"))
        threading.Thread(target=do_ngrok, daemon=True).start()

    def _build_ui(self):
        # Fejléc
        hdr = tk.Frame(self, bg="#1a1a2e")
        hdr.pack(fill=tk.X)
        tk.Label(hdr, text="dani-meglepi szerver",
                 font=("Helvetica", 15, "bold"),
                 fg="white", bg="#1a1a2e", pady=10).pack()

        # Állapot
        sf = tk.LabelFrame(self, text="Allapot", **self.PAD)
        sf.pack(fill=tk.X, padx=14, pady=(10,4))

        self._lbl_clients = tk.Label(sf, text="Kliensek: -",
                                     font=("Helvetica", 11))
        self._lbl_clients.grid(row=0, column=0, sticky="w", **self.PAD)

        self._lbl_state = tk.Label(sf, text="Allapot: tetlen",
                                   font=("Helvetica", 11))
        self._lbl_state.grid(row=0, column=1, sticky="w", **self.PAD)

        self._lbl_ngrok = tk.Label(sf, text="Ngrok: indul...",
                                   font=("Helvetica", 11), fg="orange")
        self._lbl_ngrok.grid(row=1, column=0, columnspan=2,
                              sticky="w", **self.PAD)

        self._lbl_url = tk.Label(sf,
                                 text=f"URL: wss://{NGROK_DOMAIN}/ws/<id>",
                                 font=("Courier", 9), fg="gray")
        self._lbl_url.grid(row=2, column=0, columnspan=2,
                            sticky="w", **self.PAD)

        # Vezérlők
        ctrl = tk.LabelFrame(self, text="Vezérles", **self.PAD)
        ctrl.pack(fill=tk.X, padx=14, pady=4)

        btn = dict(font=("Helvetica", 12, "bold"), width=16, height=2,
                   relief=tk.FLAT, cursor="hand2", bd=0)

        tk.Button(ctrl, text="Musor inditasa",
                  bg="#27ae60", fg="white", activebackground="#2ecc71",
                  command=self._cmd_show, **btn).grid(
                      row=0, column=0, padx=10, pady=8)

        tk.Button(ctrl, text="Demo mod",
                  bg="#e67e22", fg="white", activebackground="#f39c12",
                  command=self._cmd_demo, **btn).grid(
                      row=0, column=1, padx=10, pady=8)

        tk.Button(ctrl, text="Leallitas",
                  bg="#c0392b", fg="white", activebackground="#e74c3c",
                  command=self._cmd_clear, **btn).grid(
                      row=0, column=2, padx=10, pady=8)

        # Időzítő
        sched = tk.LabelFrame(self, text="Idozito", **self.PAD)
        sched.pack(fill=tk.X, padx=14, pady=4)

        tk.Label(sched, text="Inditas (ora:perc):").grid(
            row=0, column=0, sticky="w", **self.PAD)

        tf = tk.Frame(sched)
        tf.grid(row=0, column=1, sticky="w", **self.PAD)

        self._hour_var = tk.StringVar(value=datetime.now().strftime("%H"))
        self._min_var  = tk.StringVar(value="00")
        vcmd = (self.register(lambda v: v=="" or (v.isdigit() and len(v)<=2)), "%P")

        tk.Spinbox(tf, from_=0, to=23, width=4, format="%02.0f",
                   textvariable=self._hour_var,
                   validate="key", validatecommand=vcmd).pack(side=tk.LEFT)
        tk.Label(tf, text=":").pack(side=tk.LEFT)
        tk.Spinbox(tf, from_=0, to=59, width=4, format="%02.0f",
                   textvariable=self._min_var,
                   validate="key", validatecommand=vcmd).pack(side=tk.LEFT)

        bf = tk.Frame(sched)
        bf.grid(row=1, column=0, columnspan=2, pady=(0,6))

        tk.Button(bf, text="Idozito beallitasa",
                  command=self._set_schedule,
                  font=("Helvetica", 10), relief=tk.FLAT,
                  bg="#2980b9", fg="white", cursor="hand2",
                  padx=10, pady=4).pack(side=tk.LEFT, padx=6)

        self._btn_cancel = tk.Button(bf, text="Megszakitas",
                  command=self._cancel_schedule,
                  font=("Helvetica", 10), relief=tk.FLAT,
                  bg="#7f8c8d", fg="white", cursor="hand2",
                  padx=10, pady=4, state=tk.DISABLED)
        self._btn_cancel.pack(side=tk.LEFT, padx=6)

        self._lbl_sched = tk.Label(sched,
                                   text="Nincs beallitott idozito.",
                                   font=("Helvetica", 9), fg="gray")
        self._lbl_sched.grid(row=2, column=0, columnspan=2,
                              sticky="w", **self.PAD)

        # Kliensek lista
        lf = tk.LabelFrame(self, text="Csatlakozott kliensek", **self.PAD)
        lf.pack(fill=tk.BOTH, expand=True, padx=14, pady=4)

        self._listbox = tk.Listbox(lf, height=6,
                                   font=("Courier", 9),
                                   selectmode=tk.NONE, activestyle="none")
        sb = ttk.Scrollbar(lf, orient=tk.VERTICAL,
                           command=self._listbox.yview)
        self._listbox.config(yscrollcommand=sb.set)
        self._listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        # Napló
        logf = tk.LabelFrame(self, text="Naplo", **self.PAD)
        logf.pack(fill=tk.BOTH, expand=True, padx=14, pady=(4,12))

        self._log_text = tk.Text(logf, height=8, state=tk.DISABLED,
                                 font=("Courier", 8), bg="#f0f0f0")
        lsb = ttk.Scrollbar(logf, orient=tk.VERTICAL,
                             command=self._log_text.yview)
        self._log_text.config(yscrollcommand=lsb.set)
        self._log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        lsb.pack(side=tk.RIGHT, fill=tk.Y)

    # ── Gombok ────────────────────────────────────────────────────────────
    def _cmd_show(self):
        broadcast_sync("show")
        self._log("Musor elinditva -> osszes kliens")

    def _cmd_demo(self):
        broadcast_sync("demo")
        self._log("Demo mod -> osszes kliens")

    def _cmd_clear(self):
        broadcast_sync("clear")
        self._log("Leallitas -> osszes kliens")

    # ── Időzítő ────────────────────────────────────────────────────────────
    def _set_schedule(self):
        try:
            h = int(self._hour_var.get())
            m = int(self._min_var.get())
        except ValueError:
            messagebox.showerror("Hiba", "Ervenytelen idopont!")
            return
        now = datetime.now()
        target = now.replace(hour=h, minute=m, second=0, microsecond=0)
        if target <= now:
            target += timedelta(days=1)
        self._schedule_target = target
        delta_ms = int((target - now).total_seconds() * 1000)
        if self._schedule_job:
            self.after_cancel(self._schedule_job)
        self._schedule_job = self.after(delta_ms, self._fire_schedule)
        self._btn_cancel.config(state=tk.NORMAL)
        mins = int((target - now).total_seconds() // 60)
        self._lbl_sched.config(
            text=f"Idozitve: {target.strftime('%H:%M')} ({mins} perc mulva)",
            fg="#2980b9")
        self._log(f"Idozito: {target.strftime('%H:%M')}")

    def _cancel_schedule(self):
        if self._schedule_job:
            self.after_cancel(self._schedule_job)
            self._schedule_job = None
        self._btn_cancel.config(state=tk.DISABLED)
        self._lbl_sched.config(text="Nincs beallitott idozito.", fg="gray")
        self._log("Idozito torolve")

    def _fire_schedule(self):
        self._schedule_job = None
        self._btn_cancel.config(state=tk.DISABLED)
        self._lbl_sched.config(text="Idozito lejart!", fg="#27ae60")
        self._cmd_show()

    # ── Állapot frissítés ──────────────────────────────────────────────────
    def _refresh_status(self):
        self.after(0, self._do_refresh)

    def _do_refresh(self):
        count = manager.client_count()
        state_map = {
            "idle": "tetlen",
            "show": "musor fut",
            "demo": "demo fut",
        }
        self._lbl_clients.config(text=f"Kliensek: {count} db")
        self._lbl_state.config(
            text=f"Allapot: {state_map.get(manager.current_state, '?')}")
        self._listbox.delete(0, tk.END)
        for cid in manager.active:
            self._listbox.insert(tk.END, f"  {cid}")

    # ── Napló ──────────────────────────────────────────────────────────────
    def _log(self, msg: str):
        ts = datetime.now().strftime("%H:%M:%S")
        self._log_text.config(state=tk.NORMAL)
        self._log_text.insert(tk.END, f"[{ts}] {msg}\n")
        self._log_text.see(tk.END)
        self._log_text.config(state=tk.DISABLED)

    # ── Bezárás ────────────────────────────────────────────────────────────
    def _on_close(self):
        if messagebox.askokcancel("Kilepes", "Biztosan leallitod a szervert?"):
            broadcast_sync("clear")
            if self.ngrok_proc:
                self.ngrok_proc.terminate()
            self.after(400, self.destroy)

# ── Belépési pont ────────────────────────────────────────────────────────────
def main():
    gui = App()
    gui.mainloop()

if __name__ == "__main__":
    main()
