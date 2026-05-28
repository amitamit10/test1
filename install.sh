#!/usr/bin/env bash
# install.sh — Build and install the open bambu_networking plugin
# Supports: Ubuntu/Debian, Arch, Fedora/RHEL, and generic Linux
# Usage:  ./install.sh [--slicer orca|bambu] [--no-key] [--bambu-connect PATH]

set -euo pipefail

SLICER="orca"          # default target slicer
SKIP_KEY=false
BAMBU_CONNECT_PATH=""

# ── parse args ────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --slicer)       SLICER="$2";            shift 2 ;;
        --no-key)       SKIP_KEY=true;          shift   ;;
        --bambu-connect) BAMBU_CONNECT_PATH="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--slicer orca|bambu] [--no-key] [--bambu-connect /path/to/app.asar]"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── colours ───────────────────────────────────────────────────────────────────
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
error() { echo -e "${RED}[✗]${NC} $*"; exit 1; }

# ── detect config dir ─────────────────────────────────────────────────────────
detect_plugin_dir() {
    local candidates=()
    case "$SLICER" in
        orca)
            candidates=(
                "$HOME/.config/OrcaSlicer/plugins"
                "$HOME/.var/app/com.bambulab.BambuStudio/config/OrcaSlicer/plugins"
                "$HOME/.var/app/io.github.softfever.OrcaSlicer/config/OrcaSlicer/plugins"
            ) ;;
        bambu)
            candidates=(
                "$HOME/.config/BambuStudio/plugins"
                "$HOME/.var/app/com.bambulab.BambuStudio/config/BambuStudio/plugins"
            ) ;;
        *) error "Unknown slicer '$SLICER'. Use 'orca' or 'bambu'." ;;
    esac

    for d in "${candidates[@]}"; do
        if [[ -d "$d" || -d "$(dirname "$d")" ]]; then
            echo "$d"; return
        fi
    done

    # Fallback: first candidate
    echo "${candidates[0]}"
}

# ── install deps ──────────────────────────────────────────────────────────────
install_deps() {
    info "Installing build dependencies…"

    if command -v apt-get &>/dev/null; then
        sudo apt-get update -qq
        sudo apt-get install -y --no-install-recommends \
            cmake build-essential ninja-build \
            libmosquitto-dev libcurl4-openssl-dev libssl-dev \
            python3 python3-pip nodejs npm
    elif command -v pacman &>/dev/null; then
        sudo pacman -Sy --noconfirm \
            cmake ninja mosquitto curl openssl \
            python nodejs npm
    elif command -v dnf &>/dev/null; then
        sudo dnf install -y \
            cmake ninja-build gcc-c++ \
            mosquitto-devel libcurl-devel openssl-devel \
            python3 nodejs npm
    else
        warn "Could not detect package manager. Install manually:"
        warn "  cmake, libmosquitto-dev, libcurl-dev, libssl-dev"
    fi
}

# ── build ─────────────────────────────────────────────────────────────────────
build_plugin() {
    info "Building libbambu_networking.so…"
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$PLUGIN_DIR" \
        2>&1 | grep -v "^--"
    cmake --build build --parallel "$(nproc)"
    info "Build complete."
}

# ── install ───────────────────────────────────────────────────────────────────
detect_orca_version() {
    # Try to find the OrcaSlicer version from its binary or AppImage
    local ver=""
    for bin in /usr/bin/orcaslicer /usr/local/bin/orcaslicer \
               "$HOME/.local/bin/orcaslicer" \
               /opt/OrcaSlicer/OrcaSlicer; do
        if [[ -x "$bin" ]]; then
            ver=$("$bin" --version 2>/dev/null | grep -oP '\d+\.\d+\.\d+' | head -1) && break
        fi
    done
    # AppImage
    if [[ -z "$ver" ]]; then
        for img in "$HOME"/*.AppImage "$HOME"/Downloads/*.AppImage /opt/*.AppImage; do
            [[ -f "$img" ]] && ver=$(basename "$img" | grep -oP '\d+\.\d+\.\d+' | head -1) && break
        done
    fi
    # Fallback to our compiled version
    echo "${ver:-2.2.0}"
}

version_to_plugin_name() {
    # "2.2.0" → "02.02.00.00"  |  "2.6.1" → "02.06.01.00"
    local v="$1"
    IFS='.' read -r maj min pat <<< "$v"
    printf "%02d.%02d.%02d.00" "$maj" "$min" "$pat"
}

install_plugin() {
    info "Installing to: $PLUGIN_DIR"
    mkdir -p "$PLUGIN_DIR"

    local orca_ver; orca_ver="$(detect_orca_version)"
    local plugin_ver; plugin_ver="$(version_to_plugin_name "$orca_ver")"
    local versioned_name="libbambu_networking_${plugin_ver}.so"

    info "OrcaSlicer version : $orca_ver"
    info "Plugin filename    : $versioned_name"

    cp build/libbambu_networking.so.1.0.0 "$PLUGIN_DIR/$versioned_name"
    # Also provide unversioned symlink for direct dlopen paths
    ln -sf "$PLUGIN_DIR/$versioned_name" "$PLUGIN_DIR/libbambu_networking.so"

    # ── Patch OrcaSlicer.conf to register the plugin ──────────────────────────
    local conf_dir; conf_dir="$(dirname "$PLUGIN_DIR")"
    local conf_file="$conf_dir/OrcaSlicer.conf"
    if [[ -f "$conf_file" ]]; then
        # Remove any existing network_plugin_version line and add ours
        grep -v "network_plugin_version" "$conf_file" > "${conf_file}.tmp"
        echo "network_plugin_version = $plugin_ver" >> "${conf_file}.tmp"
        mv "${conf_file}.tmp" "$conf_file"
        info "Updated OrcaSlicer.conf → network_plugin_version = $plugin_ver"
    else
        warn "OrcaSlicer.conf not found at $conf_file"
        warn "You may need to launch OrcaSlicer once first, then re-run this script."
    fi

    info "Plugin installed: $PLUGIN_DIR/$versioned_name"
}

# ── signing key extraction ────────────────────────────────────────────────────
extract_key() {
    if $SKIP_KEY; then
        warn "Skipping key extraction (--no-key). Developer Mode required."
        return
    fi

    info "Looking for Bambu Connect to extract signing key…"

    local asar_path=""

    # User-supplied path
    if [[ -n "$BAMBU_CONNECT_PATH" ]]; then
        asar_path="$BAMBU_CONNECT_PATH"
    else
        # Auto-detect common install locations
        local candidates=(
            "$HOME/.local/share/BambuConnect/resources/app.asar"
            "$HOME/.var/app/com.bambulab.BambuConnect/data/BambuConnect/resources/app.asar"
            "/opt/BambuConnect/resources/app.asar"
            "/usr/lib/bambu-connect/resources/app.asar"
        )
        for p in "${candidates[@]}"; do
            if [[ -f "$p" ]]; then
                asar_path="$p"; break
            fi
        done
    fi

    if [[ -z "$asar_path" ]]; then
        warn "Bambu Connect not found. Skipping key extraction."
        warn "To enable non-Developer-Mode printing later, run:"
        warn "  python3 tools/extract_bambu_key.py --app /path/to/app.asar --out $PLUGIN_DIR"
        warn "Developer Mode printing still works without the key."
        return
    fi

    info "Found Bambu Connect: $asar_path"

    # Install asar if needed
    if ! command -v asar &>/dev/null && ! npx --yes @electron/asar --version &>/dev/null 2>&1; then
        info "Installing @electron/asar…"
        npm install -g @electron/asar 2>/dev/null || true
    fi

    python3 tools/extract_bambu_key.py \
        --app "$asar_path" \
        --out "$PLUGIN_DIR" && \
    info "Signing key installed — non-Developer-Mode printing enabled!" || \
    warn "Key extraction failed. Developer Mode printing still works."
}

# ── verify ────────────────────────────────────────────────────────────────────
verify() {
    info "Verifying exports…"
    local missing=0
    local critical=(
        bambu_network_create_agent
        bambu_network_destroy_agent
        bambu_network_get_version
        bambu_network_connect_printer
        bambu_network_start_local_print
        bambu_network_start_print
    )
    for sym in "${critical[@]}"; do
        if nm -D "$PLUGIN_DIR/libbambu_networking.so" 2>/dev/null | grep -q "$sym"; then
            echo -e "  ${GREEN}✓${NC} $sym"
        else
            echo -e "  ${RED}✗${NC} $sym"
            ((missing++)) || true
        fi
    done
    [[ $missing -eq 0 ]] && info "All critical symbols present." || \
                            error "$missing critical symbols missing."
}

# ── main ──────────────────────────────────────────────────────────────────────
echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║   Open Bambu Networking Installer    ║"
echo "  ║   Non-Dev-Mode printing support      ║"
echo "  ╚══════════════════════════════════════╝"
echo ""

PLUGIN_DIR="$(detect_plugin_dir)"
info "Target slicer : $SLICER"
info "Plugin dir    : $PLUGIN_DIR"
echo ""

install_deps
build_plugin
install_plugin
extract_key
verify

echo ""
echo -e "${GREEN}══════════════════════════════════════════${NC}"
echo -e "${GREEN}  Installation complete!${NC}"
echo -e "${GREEN}══════════════════════════════════════════${NC}"
echo ""
echo "  Restart $SLICER and try printing."
echo ""
if [[ ! -f "$PLUGIN_DIR/bambu_connect_private.pem" ]]; then
    echo -e "  ${YELLOW}Note:${NC} No signing key found."
    echo    "  Enable Developer Mode on your printer for now,"
    echo    "  or run: python3 tools/extract_bambu_key.py --help"
fi
echo ""
