#!/usr/bin/env node
/**
 * dani-meglepi-server
 */

"use strict";

const http    = require("http");
const https   = require("https");
const fs      = require("fs");
const path    = require("path");
const { exec, spawn } = require("child_process");
const { WebSocketServer, WebSocket } = require("ws");

// ── Konfiguráció ─────────────────────────────────────────────────────────────
const CLIENT_WS_PORT = 8765;   // kliensek csatlakoznak ide (ngrok tunnelen át)
const ADMIN_HTTP_PORT = 3000;  // admin GUI böngészőben
const NGROK_HELPER = path.join(__dirname, "ngrok-helper.py");

// ── Állapot ──────────────────────────────────────────────────────────────────
const clients = new Map();   // id → { ws, connectedAt, monitors, isAdmin }
let   state   = "idle";      // idle | show | demo
let   ngrokUrl = null;
let   ngrokProc = null;
const adminSockets = new Set(); // admin GUI WS kapcsolatok

// ── Kliens WS szerver (port 8765) ────────────────────────────────────────────
const clientWss = new WebSocketServer({ port: CLIENT_WS_PORT });

clientWss.on("connection", (ws, req) => {
  // URL: /ws/pc-XXXXXXXX
  const parts  = (req.url || "").split("/").filter(Boolean);
  const id     = parts[parts.length - 1] || "unknown";

  clients.set(id, {
    ws,
    id,
    connectedAt: new Date(),
    lastPing: new Date(),
  });

  log(`Kliens csatlakozott: ${id} (összesen: ${clients.size})`);
  broadcastAdmin({ type: "clients", data: clientList() });

  // Ha éppen megy a show/demo, az új kliens is kapja
  if (state !== "idle") {
    safeSend(ws, { cmd: state });
  }

  ws.on("message", (data) => {
    try {
      const msg = JSON.parse(data.toString());
      if (msg.ping) {
        clients.get(id) && (clients.get(id).lastPing = new Date());
      }
    } catch (_) {}
  });

  ws.on("close", () => {
    clients.delete(id);
    log(`Kliens lecsatlakozott: ${id} (összesen: ${clients.size})`);
    broadcastAdmin({ type: "clients", data: clientList() });
  });

  ws.on("error", () => clients.delete(id));
});

clientWss.on("error", (err) => log(`Kliens WS hiba: ${err.message}`));
log(`Kliens WS szerver: ws://localhost:${CLIENT_WS_PORT}`);

// ── Broadcast klienseknek ────────────────────────────────────────────────────
function broadcast(cmd) {
  state = cmd === "clear" ? "idle" : cmd;
  let sent = 0, dead = [];
  clients.forEach((c, id) => {
    if (c.ws.readyState === WebSocket.OPEN) {
      safeSend(c.ws, { cmd });
      sent++;
    } else {
      dead.push(id);
    }
  });
  dead.forEach(id => clients.delete(id));
  log(`Broadcast: "${cmd}" → ${sent} kliens`);
  broadcastAdmin({ type: "state",   data: { state } });
  broadcastAdmin({ type: "clients", data: clientList() });
  broadcastAdmin({ type: "log",     data: logEntry(`Parancs: ${cmd} → ${sent} kliens`) });
}

function safeSend(ws, obj) {
  try { ws.send(JSON.stringify(obj)); } catch (_) {}
}

// ── Admin GUI HTML ────────────────────────────────────────────────────────────
function adminHtml() {
  return `<!DOCTYPE html>
<html lang="hu">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>dani-meglepi vezérlő</title>
<style>
  :root {
    --bg: #0f0f1a; --surface: #1a1a2e; --surface2: #16213e;
    --accent: #0f3460; --green: #27ae60; --orange: #e67e22;
    --red: #c0392b; --blue: #2980b9; --text: #e0e0e0;
    --text2: #888; --border: #2a2a4a; --radius: 10px;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: var(--bg); color: var(--text);
         font-family: 'Segoe UI', system-ui, sans-serif;
         min-height: 100vh; padding: 20px; }
  h1 { text-align: center; font-size: 1.6rem; margin-bottom: 20px;
       color: #fff; letter-spacing: 1px; }
  .grid { display: grid; grid-template-columns: 1fr 1fr;
          gap: 16px; max-width: 1000px; margin: 0 auto; }
  .card { background: var(--surface); border-radius: var(--radius);
          padding: 18px; border: 1px solid var(--border); }
  .card h2 { font-size: 0.8rem; text-transform: uppercase;
              letter-spacing: 1px; color: var(--text2);
              margin-bottom: 14px; }
  /* Állapot */
  .status-row { display: flex; gap: 12px; flex-wrap: wrap; }
  .badge { padding: 6px 14px; border-radius: 20px; font-size: 0.85rem;
           font-weight: 600; }
  .badge.idle   { background: #2a2a3a; color: var(--text2); }
  .badge.show   { background: #1a3a2a; color: var(--green); }
  .badge.demo   { background: #3a2a1a; color: var(--orange); }
  .badge.conn   { background: #1a2a3a; color: #5dade2; }
  /* Vezérlők */
  .btn-group { display: flex; gap: 10px; flex-wrap: wrap; }
  .btn { padding: 12px 20px; border: none; border-radius: 8px;
         font-size: 0.95rem; font-weight: 700; cursor: pointer;
         transition: filter 0.15s; flex: 1; min-width: 120px; }
  .btn:hover { filter: brightness(1.15); }
  .btn:active { filter: brightness(0.9); }
  .btn.show  { background: var(--green);  color: #fff; }
  .btn.demo  { background: var(--orange); color: #fff; }
  .btn.clear { background: var(--red);    color: #fff; }
  /* Időzítő */
  .sched-row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  .sched-row input { background: var(--surface2); border: 1px solid var(--border);
                     color: var(--text); border-radius: 6px;
                     padding: 8px 10px; width: 60px; font-size: 1rem;
                     text-align: center; }
  .sched-row .btn { flex: 0; padding: 8px 14px; font-size: 0.85rem; }
  .sched-row .btn.sched { background: var(--blue); color: #fff; }
  .sched-row .btn.cancel { background: #555; color: #fff; }
  #sched-status { margin-top: 8px; font-size: 0.82rem; color: var(--text2); }
  /* Kliensek */
  #client-list { list-style: none; }
  #client-list li {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 8px;
    padding: 8px 10px;
    border-radius: 6px;
    background: var(--surface2);
    margin-bottom: 6px;
    font-size: 0.85rem;
    border-left: 3px solid var(--green);
  }
  #client-list li .cid { font-family: monospace; color: #5dade2; }
  #client-list li .ctime { color: var(--text2); font-size: 0.75rem;
                            align-self: center; }
  #client-list .empty { color: var(--text2); font-style: italic;
                        padding: 10px 0; font-size: 0.85rem; }
  /* Napló */
  #log { height: 220px; overflow-y: auto; font-family: monospace;
         font-size: 0.78rem; background: var(--surface2);
         border-radius: 6px; padding: 10px;
         border: 1px solid var(--border); }
  #log .entry { padding: 2px 0; border-bottom: 1px solid #1a1a2a; }
  #log .ts { color: var(--text2); margin-right: 6px; }
  /* Ngrok */
  .ngrok-url { font-family: monospace; font-size: 0.8rem;
               color: #5dade2; word-break: break-all;
               margin-top: 6px; }
  .dot { width: 8px; height: 8px; border-radius: 50%;
         display: inline-block; margin-right: 6px; }
  .dot.green { background: var(--green); }
  .dot.red   { background: var(--red); }
  .dot.orange { background: var(--orange); animation: pulse 1s infinite; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.4} }
  /* Teljes szélesség */
  .full { grid-column: 1 / -1; }
  @media (max-width: 600px) { .grid { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<h1>🎉 dani-meglepi vezérlő</h1>
<div class="grid">

  <!-- Állapot -->
  <div class="card">
    <h2>Állapot</h2>
    <div class="status-row">
      <span class="badge conn"><span id="client-count">0</span> kliens</span>
      <span class="badge idle" id="state-badge">tétlen</span>
      <span id="ngrok-badge">
        <span class="dot orange"></span>ngrok: csatlakozás...
      </span>
    </div>
    <div class="ngrok-url" id="ngrok-url"></div>
  </div>

  <!-- Vezérlők -->
  <div class="card">
    <h2>Vezérlés</h2>
    <div class="btn-group">
      <button class="btn show"  onclick="send('show')">▶ Műsor</button>
      <button class="btn demo"  onclick="send('demo')">🔴 Demo</button>
      <button class="btn clear" onclick="send('clear')">⏹ Leállítás</button>
    </div>
  </div>

  <!-- Időzítő -->
  <div class="card">
    <h2>Időzítő</h2>
    <div class="sched-row">
      <input type="number" id="sched-h" min="0" max="23" value="${new Date().getHours().toString().padStart(2,'0')}" placeholder="óó">
      <span>:</span>
      <input type="number" id="sched-m" min="0" max="59" value="00" placeholder="pp">
      <button class="btn sched"  onclick="setSchedule()">⏰ Beállít</button>
      <button class="btn cancel" onclick="cancelSchedule()" id="cancel-btn" disabled>✖</button>
    </div>
    <div id="sched-status">Nincs beállított időzítő.</div>
  </div>

  <!-- Kliensek -->
  <div class="card">
    <h2>Csatlakozott kliensek</h2>
    <ul id="client-list"><li class="empty">Nincs csatlakozott kliens.</li></ul>
  </div>

  <!-- Napló -->
  <div class="card full">
    <h2>Napló</h2>
    <div id="log"></div>
  </div>

</div>
<script>
// ── Admin WebSocket ────────────────────────────────────────────────────────
const ws = new WebSocket("ws://" + location.host + "/admin");
let schedJob = null;

ws.onopen  = () => addLog("Admin kapcsolat: OK");
ws.onclose = () => { addLog("Admin kapcsolat megszakadt — frissítsd az oldalt"); };

ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);
  switch (msg.type) {
    case "clients": updateClients(msg.data); break;
    case "state":   updateState(msg.data.state); break;
    case "log":     addLog(msg.data.text, msg.data.ts); break;
    case "ngrok":   updateNgrok(msg.data); break;
    case "init":
      updateClients(msg.data.clients);
      updateState(msg.data.state);
      updateNgrok(msg.data.ngrok);
      break;
  }
};

function send(cmd) {
  ws.send(JSON.stringify({ action: "cmd", cmd }));
}

// ── Állapot frissítés ──────────────────────────────────────────────────────
function updateState(s) {
  const badge = document.getElementById("state-badge");
  const labels = { idle: "tétlen", show: "▶ műsor fut", demo: "🔴 demo fut" };
  badge.textContent = labels[s] || s;
  badge.className = "badge " + s;
}

function updateClients(list) {
  document.getElementById("client-count").textContent = list.length;
  const ul = document.getElementById("client-list");
  if (list.length === 0) {
    ul.innerHTML = '<li class="empty">Nincs csatlakozott kliens.</li>';
    return;
  }
  ul.innerHTML = list.map(c => \`
    <li>
      <span class="cid">\${c.id}</span>
      <span class="ctime">csatl.: \${c.since}</span>
    </li>\`).join("");
}

function updateNgrok(data) {
  const badge = document.getElementById("ngrok-badge");
  const urlEl = document.getElementById("ngrok-url");
  if (data && data.url) {
    badge.innerHTML = '<span class="dot green"></span>ngrok: aktív';
    urlEl.textContent = data.url.replace("https://","wss://") + "/ws/<id>";
  } else if (data && data.error) {
    badge.innerHTML = '<span class="dot red"></span>ngrok: HIBA';
    urlEl.textContent = data.error;
  } else {
    badge.innerHTML = '<span class="dot orange"></span>ngrok: csatlakozás...';
    urlEl.textContent = "";
  }
}

// ── Időzítő ────────────────────────────────────────────────────────────────
function setSchedule() {
  const h = parseInt(document.getElementById("sched-h").value);
  const m = parseInt(document.getElementById("sched-m").value);
  if (isNaN(h)||isNaN(m)||h<0||h>23||m<0||m>59) {
    alert("Érvénytelen időpont!"); return;
  }
  const now = new Date();
  const target = new Date();
  target.setHours(h, m, 0, 0);
  if (target <= now) target.setDate(target.getDate() + 1);
  const diff = target - now;
  const mins = Math.floor(diff / 60000);
  if (schedJob) clearTimeout(schedJob);
  schedJob = setTimeout(() => {
    send("show");
    document.getElementById("sched-status").textContent = "Időzítő lejárt — műsor elindítva!";
    document.getElementById("cancel-btn").disabled = true;
    schedJob = null;
  }, diff);
  const pad = n => String(n).padStart(2,"0");
  document.getElementById("sched-status").textContent =
    \`Időzítve: \${pad(h)}:\${pad(m)} (\${mins} perc múlva)\`;
  document.getElementById("cancel-btn").disabled = false;
}

function cancelSchedule() {
  if (schedJob) { clearTimeout(schedJob); schedJob = null; }
  document.getElementById("sched-status").textContent = "Nincs beállított időzítő.";
  document.getElementById("cancel-btn").disabled = true;
}

// ── Napló ──────────────────────────────────────────────────────────────────
function addLog(text, ts) {
  const log = document.getElementById("log");
  const now = ts || new Date().toLocaleTimeString("hu-HU");
  const div = document.createElement("div");
  div.className = "entry";
  div.innerHTML = \`<span class="ts">\${now}</span>\${escHtml(text)}\`;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

function escHtml(s) {
  return String(s)
    .replace(/&/g,"&amp;").replace(/</g,"&lt;")
    .replace(/>/g,"&gt;").replace(/"/g,"&quot;");
}
</script>
</body>
</html>`;
}

// ── HTTP + Admin WS szerver (port 3000) ──────────────────────────────────────
const httpServer = http.createServer((req, res) => {
  res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
  res.end(adminHtml());
});

const adminWss = new WebSocketServer({ server: httpServer });

adminWss.on("connection", (ws) => {
  adminSockets.add(ws);

  // Kezdeti állapot küldése
  ws.send(JSON.stringify({
    type: "init",
    data: {
      clients: clientList(),
      state: { state },
      ngrok: ngrokUrl ? { url: ngrokUrl } : null,
    }
  }));

  ws.on("message", (data) => {
    try {
      const msg = JSON.parse(data.toString());
      if (msg.action === "cmd" && msg.cmd) {
        broadcast(msg.cmd);
      }
    } catch (_) {}
  });

  ws.on("close", () => adminSockets.delete(ws));
});

httpServer.listen(ADMIN_HTTP_PORT, "localhost", () => {
  log(`Admin GUI: http://localhost:${ADMIN_HTTP_PORT}`);
  // Böngésző automatikus megnyitása
  const cmd = process.platform === "linux" ? "xdg-open" : "open";
  exec(`${cmd} http://localhost:${ADMIN_HTTP_PORT}`);
});

// ── Admin broadcast ──────────────────────────────────────────────────────────
function broadcastAdmin(msg) {
  const str = JSON.stringify(msg);
  adminSockets.forEach(ws => {
    if (ws.readyState === WebSocket.OPEN) {
      try { ws.send(str); } catch (_) {}
    }
  });
}

// ── Kliens lista ─────────────────────────────────────────────────────────────
function clientList() {
  return Array.from(clients.values()).map(c => ({
    id: c.id,
    since: c.connectedAt.toLocaleTimeString("hu-HU"),
    lastPing: c.lastPing.toLocaleTimeString("hu-HU"),
  }));
}

// ── Napló ────────────────────────────────────────────────────────────────────
function logEntry(text) {
  const ts = new Date().toLocaleTimeString("hu-HU");
  return { text, ts };
}

function log(text) {
  const entry = logEntry(text);
  console.log(`[${entry.ts}] ${text}`);
  broadcastAdmin({ type: "log", data: entry });
}

// ── Ngrok indítása (Python helper subprocess) ─────────────────────────────────
function startNgrok() {
  log("Ngrok indítása (Python helper)...");

  ngrokProc = spawn("python3", [NGROK_HELPER, String(CLIENT_WS_PORT)], {
    stdio: ["ignore", "pipe", "pipe"]
  });

  ngrokProc.stdout.on("data", (data) => {
    const url = data.toString().trim();
    if (url.startsWith("http")) {
      ngrokUrl = url;
      log(`Ngrok tunnel aktív: ${url}`);
      broadcastAdmin({ type: "ngrok", data: { url } });
    }
  });

  ngrokProc.stderr.on("data", (data) => {
    const msg = data.toString().trim();
    if (msg) log(`Ngrok: ${msg}`);
  });

  ngrokProc.on("exit", (code) => {
    if (code !== 0) {
      log(`Ngrok leállt (kód: ${code}) — újraindítás 5s múlva...`);
      broadcastAdmin({ type: "ngrok", data: { error: "Leállt, újraindítás..." } });
      setTimeout(startNgrok, 5000);
    }
  });
}

startNgrok();

// ── Graceful shutdown ────────────────────────────────────────────────────────
process.on("SIGINT", () => {
  log("Leállítás...");
  broadcast("clear");
  if (ngrokProc) ngrokProc.kill();
  process.exit(0);
});
