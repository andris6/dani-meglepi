package main

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"io"
	"log"
	"math/big"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

var clients = make(map[*websocket.Conn]bool)
var broadcast = make(chan []byte)

func main() {
	setupEnvironment()

	go broadcaster()

	http.Handle("/", http.FileServer(http.Dir("./public")))
	http.HandleFunc("/ws", handleSignaling)
	http.HandleFunc("/upload", handleUpload)

	localIP := getLocalIP()
	port := "8080"

	fmt.Println("\n--- dani-meglepi-camstream ---")
	fmt.Printf("PC (Viewer):   https://localhost:%s\n", port)
	fmt.Printf("Phone (Cam):   https://%s:%s#camera\n", localIP, port)
	fmt.Println("----------------------------------------")

	log.Fatal(http.ListenAndServeTLS(":"+port, "cert.pem", "key.pem", nil))
}

func setupEnvironment() {
	if _, err := os.Stat("public"); os.IsNotExist(err) {
		os.Mkdir("public", 0755)
	}

	htmlPath := filepath.Join("public", "index.html")
	if _, err := os.Stat(htmlPath); os.IsNotExist(err) {
		os.WriteFile(htmlPath, []byte(htmlContent), 0644)
		fmt.Println("[+] Created public/index.html")
	}

	if _, err := os.Stat("cert.pem"); os.IsNotExist(err) {
		generateCerts()
		fmt.Println("[+] Generated self-signed SSL certificates")
	}
}

func generateCerts() {
	priv, _ := rsa.GenerateKey(rand.Reader, 4096)
	template := x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject: pkix.Name{
			Organization: []string{"dani-meglepi-camstream"},
		},
		NotBefore:             time.Now(),
		NotAfter:              time.Now().AddDate(10, 0, 0),
		KeyUsage:              x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
	}

	derBytes, _ := x509.CreateCertificate(rand.Reader, &template, &template, &priv.PublicKey, priv)
	
	certOut, _ := os.Create("cert.pem")
	pem.Encode(certOut, &pem.Block{Type: "CERTIFICATE", Bytes: derBytes})
	certOut.Close()

	keyOut, _ := os.OpenFile("key.pem", os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	pem.Encode(keyOut, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(priv)})
	keyOut.Close()
}


func handleSignaling(w http.ResponseWriter, r *http.Request) {
	ws, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer ws.Close()
	clients[ws] = true
	for {
		_, msg, err := ws.ReadMessage()
		if err != nil {
			delete(clients, ws)
			break
		}
		broadcast <- msg
	}
}

func broadcaster() {
	for {
		msg := <-broadcast
		for client := range clients {
			client.WriteMessage(websocket.TextMessage, msg)
		}
	}
}

func handleUpload(w http.ResponseWriter, r *http.Request) {
	file, _, err := r.FormFile("video")
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	defer file.Close()
	f, _ := os.Create(fmt.Sprintf("recording_%d.webm", time.Now().Unix()))
	defer f.Close()
	io.Copy(f, file)
	w.Write([]byte("OK"))
}

func getLocalIP() string {
	addrs, _ := net.InterfaceAddrs()
	for _, address := range addrs {
		if ipnet, ok := address.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
			if ipnet.IP.To4() != nil {
				return ipnet.IP.String()
			}
		}
	}
	return "localhost"
}


const htmlContent = `
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>dani-meglepi-camstream</title>
    <style>
        :root { --primary: #007bff; --danger: #ff4757; --bg: #121212; --card: #1e1e1e; --text: #ffffff; }
        body { font-family: sans-serif; background-color: var(--bg); color: var(--text); margin: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; }
        .container { width: 90%; max-width: 900px; background: var(--card); padding: 20px; border-radius: 15px; text-align: center; }
        video { width: 100%; background: #000; border-radius: 10px; aspect-ratio: 16/9; object-fit: cover; }
        .status-bar { margin: 15px 0; padding: 10px; background: rgba(255,255,255,0.05); font-family: monospace; font-size: 0.8rem; }
        .btn { padding: 12px 25px; border: none; border-radius: 8px; font-weight: bold; cursor: pointer; margin: 5px; }
        .btn-primary { background: var(--primary); color: white; }
        .btn-record { background: var(--danger); color: white; display: none; }
        .recording { animation: pulse 1s infinite; }
        @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }
    </style>
</head>
<body>
    <div class="container">
        <h1>dani-meglepi-camstream</h1>
        <div id="status" class="status-bar">Connecting...</div>
        <video id="remoteVideo" autoplay playsinline muted></video>
        <div class="controls">
            <button id="record-btn" class="btn btn-record">Start Recording</button>
            <button id="camera-start-btn" class="btn btn-primary" style="display:none;">Enable Camera</button>
        </div>
    </div>
    <script>
        const statusDiv = document.getElementById('status');
        const remoteVideo = document.getElementById('remoteVideo');
        const recordBtn = document.getElementById('record-btn');
        const cameraBtn = document.getElementById('camera-start-btn');
        const ws = new WebSocket("wss://" + location.host + "/ws");
        const isCamera = window.location.hash === "#camera";
        let pc = new RTCPeerConnection({ iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] });
        let mediaRecorder, recordedChunks = [], candidateQueue = [], cameraActive = false;

        function updateStatus(msg) { statusDiv.innerText = msg; }
        async function addIce(c) { 
            if (pc.remoteDescription) await pc.addIceCandidate(new RTCIceCandidate(c)); 
            else candidateQueue.push(c); 
        }
        pc.onicecandidate = e => e.candidate && ws.send(JSON.stringify({ ice: e.candidate }));

        if (isCamera) {
            cameraBtn.style.display = "block";
            cameraBtn.onclick = async () => {
                const s = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" }, audio: true });
                s.getTracks().forEach(t => pc.addTrack(t, s));
                remoteVideo.srcObject = s;
                cameraActive = true;
                ws.send(JSON.stringify({ type: 'camera_ready' }));
                cameraBtn.style.display = "none";
                updateStatus("Streaming...");
            };
            ws.onmessage = async (m) => {
                const d = JSON.parse(m.data);
                if (d.type === 'viewer_joined' && cameraActive) ws.send(JSON.stringify({ type: 'camera_ready' }));
                if (d.offer) {
                    await pc.setRemoteDescription(new RTCSessionDescription(d.offer));
                    const a = await pc.createAnswer();
                    await pc.setLocalDescription(a);
                    ws.send(JSON.stringify({ answer: a }));
                    while(candidateQueue.length) await pc.addIceCandidate(new RTCIceCandidate(candidateQueue.shift()));
                } else if (d.ice) await addIce(d.ice);
            };
        } else {
            recordBtn.style.display = "inline-block";
            pc.ontrack = e => remoteVideo.srcObject = e.streams[0];
            ws.onopen = () => ws.send(JSON.stringify({ type: 'viewer_joined' }));
            ws.onmessage = async (m) => {
                const d = JSON.parse(m.data);
                if (d.type === 'camera_ready') {
                    const o = await pc.createOffer({ offerToReceiveVideo: true, offerToReceiveAudio: true });
                    await pc.setLocalDescription(o);
                    ws.send(JSON.stringify({ offer: o }));
                } else if (d.answer) {
                    await pc.setRemoteDescription(new RTCSessionDescription(d.answer));
                    while(candidateQueue.length) await pc.addIceCandidate(new RTCIceCandidate(candidateQueue.shift()));
                } else if (d.ice) await addIce(d.ice);
            };
            recordBtn.onclick = () => {
                if (mediaRecorder && mediaRecorder.state === "recording") {
                    mediaRecorder.stop();
                    recordBtn.classList.remove("recording");
                    recordBtn.innerText = "Start Recording";
                } else {
                    recordedChunks = [];
                    mediaRecorder = new MediaRecorder(remoteVideo.srcObject);
                    mediaRecorder.ondataavailable = e => recordedChunks.push(e.data);
                    mediaRecorder.onstop = async () => {
                        const b = new Blob(recordedChunks, { type: 'video/webm' });
                        const fd = new FormData(); fd.append('video', b);
                        updateStatus("Saving...");
                        await fetch('/upload', { method: 'POST', body: fd });
                        updateStatus("Saved!");
                    };
                    mediaRecorder.start();
                    recordBtn.classList.add("recording");
                    recordBtn.innerText = "Stop Recording";
                }
            };
        }
    </script>
</body>
</html>
`

