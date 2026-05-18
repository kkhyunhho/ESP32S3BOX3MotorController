#!/usr/bin/env bash
# setup_docker.sh
# Minimal ESP-IDF firmware build environment setup for inside a Docker
# container. USB-related work (ftdi_sio unbind, device nodes, pip packages)
# is handled on every run by launch_bridge.sh, not here.
#
# Usage:  bash setup_docker.sh
# After:  source ~/.espressif/v6.0.1/esp-idf/export.sh
#         idf.py set-target esp32s3
#         idf.py build

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────
IDF_BRANCH="release/v6.0"
IDF_DIR="${HOME}/.espressif/v6.0.1/esp-idf"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ─────────────────────────────────────────────────────────────────────────

C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_RED=$'\033[31m'
C_CYAN=$'\033[36m';  C_BOLD=$'\033[1m';   C_RESET=$'\033[0m'
step()  { printf '\n%s==> %s%s\n' "${C_BOLD}${C_CYAN}" "$*" "${C_RESET}"; }
ok()    { printf '%s   v %s%s\n' "${C_GREEN}" "$*" "${C_RESET}"; }
warn()  { printf '%s   ! %s%s\n' "${C_YELLOW}" "$*" "${C_RESET}"; }
fail()  { printf '%s   x %s%s\n' "${C_RED}" "$*" "${C_RESET}"; exit 1; }

# ── sudo helper (no-op if already root, prefix sudo otherwise) ──────────
if [[ "$EUID" -eq 0 ]]; then
    SUDO=""
    ok "running as root (Docker environment - expected)"
else
    if ! command -v sudo >/dev/null 2>&1; then
        fail "not root and sudo not available - cannot install packages"
    fi
    SUDO="sudo"
fi

# ── Active venv warning ─────────────────────────────────────────────────
if [[ -n "${VIRTUAL_ENV:-}" ]]; then
    warn "A Python venv is active (${VIRTUAL_ENV})"
    warn "ESP-IDF install.sh creates its own venv. To avoid conflicts,"
    warn "  run ${C_BOLD}deactivate${C_RESET}${C_YELLOW} first and re-run this script."
    read -r -p "Continue anyway? [y/N] " yn
    [[ "${yn}" =~ ^[Yy]$ ]] || exit 0
fi


step "1/3  Install APT packages"
${SUDO} apt-get update -qq
${SUDO} apt-get install -y \
    git wget curl flex bison gperf cmake ninja-build ccache \
    libffi-dev libssl-dev dfu-util libusb-1.0-0 \
    python3 python3-pip python3-venv \
    build-essential pkg-config
ok "base packages installed"


step "2/3  Install ESP-IDF (${IDF_BRANCH}) into ${IDF_DIR}"
mkdir -p "$(dirname "${IDF_DIR}")"
if [[ -d "${IDF_DIR}/.git" ]]; then
    ok "${IDF_DIR} already exists - skipping clone"
else
    git clone -b "${IDF_BRANCH}" --recursive https://github.com/espressif/esp-idf.git "${IDF_DIR}"
    ok "ESP-IDF cloned"
fi
(cd "${IDF_DIR}" && ./install.sh esp32s3)
ok "ESP-IDF toolchain installed"


step "3/3  Verify install"
# shellcheck disable=SC1091
source "${IDF_DIR}/export.sh" >/dev/null
idf.py --version
ok "idf.py invocation succeeded"


cat <<EOF

${C_BOLD}${C_GREEN}===============================================================
  Build environment install complete.
===============================================================${C_RESET}

  ${C_BOLD}Activate ESP-IDF in every new terminal:${C_RESET}
     source ${IDF_DIR}/export.sh

  ${C_BOLD}Build:${C_RESET}
     cd ${PROJECT_DIR}
     idf.py set-target esp32s3      # first time only
     idf.py build

  ${C_BOLD}Convenience alias (optional, add to ~/.bashrc):${C_RESET}
     alias get_idf='source ${IDF_DIR}/export.sh'

  ${C_BOLD}${C_YELLOW}Note:${C_RESET}
     ${C_BOLD}Flashing firmware / running the bridge${C_RESET} only works when the
     container has USB passthrough configured. If it does:
     - idf.py -p /dev/ttyACM0 flash monitor
     - ./launch_bridge.sh        (auto-installs pyserial/pyftdi, unbinds
                                  ftdi_sio, repairs /dev/bus/usb nodes,
                                  then runs bridge.py)
     For permanent host-side setup (udev rules, etc.) see SETUP_UBUNTU.md.

EOF
