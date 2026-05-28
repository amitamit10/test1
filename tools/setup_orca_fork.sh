#!/usr/bin/env bash
# setup_orca_fork.sh
# Forks OrcaSlicer to your GitHub account, adds our plugin as a submodule,
# and creates a ready-to-build fork with the open bambu_networking plugin bundled.
#
# Prerequisites:
#   - GitHub CLI installed and logged in: https://cli.github.com
#     (run: gh auth login)
#   - Git configured with your name/email
#
# Usage: bash tools/setup_orca_fork.sh

set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info() { echo -e "${GREEN}[+]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }

PLUGIN_REPO="https://github.com/amitamit10/test1"   # ← our plugin repo
ORCA_UPSTREAM="OrcaSlicer/OrcaSlicer"
FORK_DIR="$HOME/OrcaSlicer-bambu"

# ── 1. Check gh CLI ────────────────────────────────────────────────────────────
if ! command -v gh &>/dev/null; then
    echo "GitHub CLI (gh) not found. Install it from https://cli.github.com"
    echo "Then run: gh auth login"
    exit 1
fi

GH_USER=$(gh api user --jq '.login')
info "Logged in as: $GH_USER"
FORK_REMOTE="https://github.com/$GH_USER/OrcaSlicer"

# ── 2. Fork OrcaSlicer ────────────────────────────────────────────────────────
info "Forking OrcaSlicer/OrcaSlicer → $GH_USER/OrcaSlicer …"
gh repo fork "$ORCA_UPSTREAM" --clone=false 2>/dev/null || \
    warn "Fork already exists, continuing…"

# ── 3. Clone the fork ─────────────────────────────────────────────────────────
if [[ -d "$FORK_DIR" ]]; then
    warn "Directory $FORK_DIR already exists. Pulling latest…"
    git -C "$FORK_DIR" pull --ff-only
else
    info "Cloning fork to $FORK_DIR (this will take a few minutes)…"
    git clone "$FORK_REMOTE" "$FORK_DIR" --depth=1
fi

cd "$FORK_DIR"

# ── 4. Create integration branch ─────────────────────────────────────────────
BRANCH="feature/open-bambu-networking"
git checkout -B "$BRANCH" 2>/dev/null || git checkout "$BRANCH"
info "Working on branch: $BRANCH"

# ── 5. Add our plugin as a submodule ─────────────────────────────────────────
if [[ ! -d "bambu-networking/.git" ]]; then
    info "Adding open bambu-networking plugin as submodule…"
    git submodule add "$PLUGIN_REPO" bambu-networking
    git submodule update --init
else
    info "Submodule already present, updating…"
    git submodule update --remote bambu-networking
fi

# ── 6. Write CMake integration ────────────────────────────────────────────────
info "Writing CMake integration…"
cat > bambu-networking/CMakeIntegration.cmake << 'CMAKE'
# Builds the open bambu_networking plugin and installs it to
# <build>/plugins/ so OrcaSlicer finds it at runtime.

if(NOT DEFINED BAMBU_PLUGIN_VERSION)
    set(BAMBU_PLUGIN_VERSION "02.06.00.00")
endif()

include(ExternalProject)

ExternalProject_Add(open_bambu_networking
    SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/bambu-networking"
    BINARY_DIR  "${CMAKE_BINARY_DIR}/bambu-networking-build"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/plugins
        -DBAMBU_PLUGIN_VERSION=${BAMBU_PLUGIN_VERSION}
    BUILD_ALWAYS ON
    INSTALL_COMMAND
        cmake --install "${CMAKE_BINARY_DIR}/bambu-networking-build"
)

# OrcaSlicer looks for versioned plugin name
set(BAMBU_PLUGIN_SO
    "${CMAKE_BINARY_DIR}/plugins/lib/libbambu_networking.so.1.0.0"
    CACHE INTERNAL "")
CMAKE

# ── 7. Patch the top-level CMakeLists.txt ────────────────────────────────────
info "Patching CMakeLists.txt to build our plugin…"
CMAKELISTS="CMakeLists.txt"
MARKER="# === open-bambu-networking integration ==="

if ! grep -q "$MARKER" "$CMAKELISTS"; then
    # Append near the end, before the last closing lines
    cat >> "$CMAKELISTS" << PATCH

$MARKER
# Build the open-source bambu_networking plugin as part of OrcaSlicer.
# Remove this block to revert to downloading the proprietary plugin.
if(EXISTS "\${CMAKE_CURRENT_SOURCE_DIR}/bambu-networking/CMakeLists.txt")
    message(STATUS "Building open bambu_networking plugin")
    include(bambu-networking/CMakeIntegration.cmake)
endif()
PATCH
    info "CMakeLists.txt patched."
else
    info "CMakeLists.txt already patched."
fi

# ── 8. Write a build + install helper ────────────────────────────────────────
info "Writing build-fork.sh…"
cat > build-fork.sh << 'BUILDSCRPT'
#!/usr/bin/env bash
# build-fork.sh — Build OrcaSlicer with bundled open bambu_networking plugin.
# Run this from the fork root after setup_orca_fork.sh.

set -euo pipefail

# Install OrcaSlicer build deps (Ubuntu/Debian)
if command -v apt-get &>/dev/null; then
    sudo apt-get install -y \
        cmake ninja-build git build-essential \
        libgtk-3-dev libwebkit2gtk-4.0-dev \
        libglu1-mesa-dev freeglut3-dev \
        libcurl4-openssl-dev libssl-dev \
        libmosquitto-dev \
        python3 npm
fi

mkdir -p build && cd build

cmake .. \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBAMBU_PLUGIN_VERSION="02.06.00.00"

ninja -j"$(nproc)"

echo ""
echo "Build complete. Run OrcaSlicer with:"
echo "  ./build/OrcaSlicer"
BUILDSCRPT
chmod +x build-fork.sh

# ── 9. Commit ────────────────────────────────────────────────────────────────
git add -A
git commit -m "Add open bambu_networking plugin as submodule

Replaces the proprietary libbambu_networking.so with our open-source
clean-room reimplementation. Supports:
  - LAN printing (MQTT + FTPS)
  - Cloud printing (Bambu cloud REST + MQTT)
  - Non-Developer-Mode signing (RSA-SHA256, loads key from config dir)
  - SSDP printer discovery

See bambu-networking/docs/PROTOCOL.md for protocol details.
See bambu-networking/tools/extract_bambu_key.py to enable non-Dev-Mode."

# ── 10. Push ──────────────────────────────────────────────────────────────────
info "Pushing branch $BRANCH to your fork…"
git push -u origin "$BRANCH"

echo ""
echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Fork ready!${NC}"
echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
echo ""
echo "  Fork URL : https://github.com/$GH_USER/OrcaSlicer/tree/$BRANCH"
echo ""
echo "  To build:"
echo "    cd $FORK_DIR"
echo "    bash build-fork.sh"
echo ""
echo "  To open a PR to SFC's baltobu fork:"
echo "    gh pr create --repo f.sfconservancy.org/baltobu/orca-slicer-for-bambu"
echo ""
