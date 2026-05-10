#!/bin/bash
# ================================================================
#  dani-meglepi — Google Cloud Run deploy szkript
# ================================================================

set -e

# ── Konfiguráció ──────────────────────────────────────────────────
SERVICE_NAME="dani-meglepi"
REGION="europe-west1"          # Belgiumban van, iskolai használathoz jó latencia
IMAGE="gcr.io/$(gcloud config get-value project)/${SERVICE_NAME}"

echo ""
echo "================================================"
echo "  dani-meglepi — Cloud Run deploy"
echo "  Projekt: $(gcloud config get-value project)"
echo "  Region:  ${REGION}"
echo "================================================"
echo ""

# ── 1. Docker image build + push ─────────────────────────────────
echo "[1/3] Docker image építése és feltöltése..."
gcloud builds submit \
  --tag "${IMAGE}" \
  --quiet

echo "  OK: image feltöltve → ${IMAGE}"

# ── 2. Cloud Run deploy ───────────────────────────────────────────
echo ""
echo "[2/3] Cloud Run deploy..."
gcloud run deploy "${SERVICE_NAME}" \
  --image "${IMAGE}" \
  --platform managed \
  --region "${REGION}" \
  --allow-unauthenticated \
  --port 8080 \
  --memory 256Mi \
  --cpu 1 \
  --min-instances 1 \
  --max-instances 1 \
  --timeout 3600 \
  --session-affinity \
  --quiet

# ── 3. URL lekérése ───────────────────────────────────────────────
echo ""
echo "[3/3] Szerver URL lekérése..."
URL=$(gcloud run services describe "${SERVICE_NAME}" \
  --platform managed \
  --region "${REGION}" \
  --format "value(status.url)")

WS_URL="${URL/https:\/\//wss://}"

echo ""
echo "================================================"
echo "  Deploy KÉSZ!"
echo ""
echo "  Admin GUI : ${URL}"
echo "  Kliens WS : ${WS_URL}/ws/<pc-id>"
echo ""
echo "  FONTOS: A client.c-ben ezt az URL-t kell"
echo "  beállítani WS_HOST-ként:"
echo "  ${URL/https:\/\//}"
echo "================================================"
echo ""

# URL mentése fájlba (kliens újrafordításhoz)
echo "${URL/https:\/\//}" > .cloudrun-url
echo "URL elmentve: .cloudrun-url"
