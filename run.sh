#!/usr/bin/env bash
# Launch build (optional), server, and client for UDS-OB-HFT.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

SYMBOL="${UDS_SYMBOL:-BTC}"
DO_BUILD=1
DO_FETCH=0
SERVER_PID=""

usage() {
    cat <<'EOF'
Usage: ./run.sh [options]

Starts the market data server in the background, then runs the client in the
foreground. Stops the server when the client exits (Ctrl+C).

Options:
  -h, --help          Show this help
  -s, --symbol SYM    Subscribe to SYM (default: BTC, or UDS_SYMBOL)
  --no-build          Skip cmake build
  --fetch             Download Kaggle CSVs first (requires .venv + credentials)
  --server-only       Start server only (no client)
  --client-only       Start client only (server must already be running)

Environment (passed through to server/client):
  UDS_DATA_DIR, UDS_SERVER_HOST, UDS_SERVER_PORT, UDS_REPLAY_SPEED, UDS_LOOP

Examples:
  ./run.sh
  ./run.sh --symbol ETH
  ./run.sh --fetch --no-build
  ./run.sh --server-only
EOF
}

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

wait_for_server() {
    local host="${UDS_SERVER_HOST:-127.0.0.1}"
    local port="${UDS_SERVER_PORT:-9000}"
    local attempts=0
    local max_attempts=600

    echo "[run] Waiting for server on ${host}:${port} - CSV load may take a minute..."
    while [[ "$attempts" -lt "$max_attempts" ]]; do
        if nc -z "$host" "$port" 2>/dev/null; then
            echo "[run] Server is ready."
            return 0
        fi
        if [[ -n "$SERVER_PID" ]] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "[run] Server exited during startup." >&2
            return 1
        fi
        sleep 0.5
        attempts=$((attempts + 1))
    done

    echo "[run] Timeout waiting for server ${host}:${port}." >&2
    return 1
}

trap cleanup EXIT INT TERM

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -s|--symbol)
            SYMBOL="$2"
            shift 2
            ;;
        --no-build)
            DO_BUILD=0
            shift
            ;;
        --fetch)
            DO_FETCH=1
            shift
            ;;
        --server-only)
            MODE="server"
            shift
            ;;
        --client-only)
            MODE="client"
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

MODE="${MODE:-all}"

# --- Build ---
if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "[run] Building..."
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build
fi

if [[ ! -x "$ROOT/build/server" ]] || [[ ! -x "$ROOT/build/client" ]]; then
    echo "[run] Missing build/server or build/client. Run without --no-build." >&2
    exit 1
fi

if [[ "$DO_FETCH" -eq 1 ]]; then
    if [[ ! -d "$ROOT/.venv" ]]; then
        echo "[run] .venv not found. Create it and: pip install -r requirements.txt" >&2
        exit 1
    fi
    echo "[run] Fetching Kaggle data..."
    # shellcheck source=/dev/null
    source "$ROOT/.venv/bin/activate"
    python "$ROOT/bin/scripts/python/data_fetcher.py"
fi

# --- Server (background) + optional port wait ---
if [[ "$MODE" == "all" || "$MODE" == "server" ]]; then
    echo "[run] Starting server (port ${UDS_SERVER_PORT:-9000})..."
    "$ROOT/build/server" &
    SERVER_PID=$!
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[run] Server failed to start." >&2
        exit 1
    fi
fi

if [[ "$MODE" == "all" ]]; then
    wait_for_server
fi

if [[ "$MODE" == "server" ]]; then
    echo "[run] Server PID $SERVER_PID - Ctrl+C to stop."
    wait "$SERVER_PID"
    SERVER_PID=""
    exit 0
fi

export UDS_SYMBOL="$SYMBOL"
echo "[run] Starting client symbol=$UDS_SYMBOL ..."
"$ROOT/build/client"
SERVER_PID=""
