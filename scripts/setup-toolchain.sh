#!/usr/bin/env bash
# setup-toolchain.sh — one-time toolchain setup for beeros development
#
# Installs:
#   riscv64-elf-gcc   cross compiler (Homebrew)
#   qemu              system emulator (Homebrew)
#   picolibc          bare-metal C library, built from source → /opt/picolibc-rv64
#
# Run once per machine, then just use `make BOARD=virt-riscv`.
# Safe to re-run: checks what's already installed before doing work.
#
# For AArch64 support you also need the ARM toolchain:
#   brew install --cask gcc-arm-embedded   # aarch64-none-elf-gcc

set -euo pipefail

PICOLIBC_PREFIX="${PICOLIBC_PREFIX:-/opt/picolibc-rv64}"
PICOLIBC_REPO="https://github.com/picolibc/picolibc"
BUILD_DIR="$(mktemp -d)/picolibc-build"

info()  { echo "  [setup] $*"; }
ok()    { echo "  [setup] ✓ $*"; }
die()   { echo "  [setup] ERROR: $*" >&2; exit 1; }

# ── 1. Homebrew tools ──────────────────────────────────────────────────
info "Checking Homebrew packages..."

need_brew() {
    brew list "$1" &>/dev/null || brew install "$1"
}

need_brew riscv64-elf-gcc
need_brew qemu
need_brew meson
need_brew ninja
need_brew python3
need_brew git
ok "Homebrew packages present"

# ── 2. picolibc ───────────────────────────────────────────────────────
if [ -f "${PICOLIBC_PREFIX}/lib/picolibc.specs" ]; then
    ok "picolibc already installed at ${PICOLIBC_PREFIX}"
    echo ""
    echo "  All done. Build beeros with:"
    echo "    make BOARD=virt-riscv"
    echo "    make run  BOARD=virt-riscv"
    exit 0
fi

info "Building picolibc → ${PICOLIBC_PREFIX}"
info "This takes about 10 minutes on first run."
echo ""

# Check we can write to the prefix (or will need sudo)
NEED_SUDO=0
if [ ! -d "${PICOLIBC_PREFIX}" ]; then
    parent="$(dirname "${PICOLIBC_PREFIX}")"
    [ -w "${parent}" ] || NEED_SUDO=1
fi

info "Cloning picolibc..."
git clone --depth=1 "${PICOLIBC_REPO}" "${BUILD_DIR}/src"

info "Configuring (rv64gc / lp64d)..."
mkdir -p "${BUILD_DIR}/build"
cd "${BUILD_DIR}/build"

# do-riscv-configure builds a multilib picolibc for all standard RISC-V ABIs.
# We set the prefix and disable tests to keep the build fast.
python3 "${BUILD_DIR}/src/scripts/do-riscv-configure" \
    -Dprefix="${PICOLIBC_PREFIX}" \
    -Dtests=false \
    -Dpicolib=false

info "Building..."
ninja -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

info "Installing to ${PICOLIBC_PREFIX}..."
if [ "${NEED_SUDO}" -eq 1 ]; then
    echo "  (sudo required to write to ${PICOLIBC_PREFIX})"
    sudo ninja install
else
    ninja install
fi

cd /
rm -rf "${BUILD_DIR}"

echo ""
ok "picolibc installed at ${PICOLIBC_PREFIX}"
echo ""
echo "  All done. Build beeros with:"
echo "    make BOARD=virt-riscv"
echo "    make run  BOARD=virt-riscv"
