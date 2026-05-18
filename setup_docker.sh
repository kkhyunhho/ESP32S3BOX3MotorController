#!/usr/bin/env bash
# setup_docker.sh
# Docker 컨테이너 안에서 ESP-IDF 펌웨어 빌드 환경만 세팅하는 축소판.
# (PC 브리지 / USB / udev / dialout 관련 단계는 의도적으로 빠져 있음 —
#  그 작업들은 호스트 NUC 에서 setup_ubuntu.sh 로 따로 진행)
#
# 사용:  bash setup_docker.sh
# 종료 후 ▶︎ source ~/esp/esp-idf/export.sh
#         ▶︎ idf.py set-target esp32s3
#         ▶︎ idf.py build

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────
IDF_BRANCH="release/v5.3"
IDF_DIR="${HOME}/esp/esp-idf"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ─────────────────────────────────────────────────────────────────────────

C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_RED=$'\033[31m'
C_CYAN=$'\033[36m';  C_BOLD=$'\033[1m';   C_RESET=$'\033[0m'
step()  { printf '\n%s==> %s%s\n' "${C_BOLD}${C_CYAN}" "$*" "${C_RESET}"; }
ok()    { printf '%s   ✓ %s%s\n' "${C_GREEN}" "$*" "${C_RESET}"; }
warn()  { printf '%s   ! %s%s\n' "${C_YELLOW}" "$*" "${C_RESET}"; }
fail()  { printf '%s   ✗ %s%s\n' "${C_RED}" "$*" "${C_RESET}"; exit 1; }

# ── sudo 헬퍼 (root 면 그냥 실행, 아니면 sudo 붙임) ────────────────────
if [[ "$EUID" -eq 0 ]]; then
    SUDO=""
    ok "root 로 실행 중 (Docker 환경 — 정상)"
else
    if ! command -v sudo >/dev/null 2>&1; then
        fail "root 가 아니고 sudo 도 없음 — 패키지 설치 불가"
    fi
    SUDO="sudo"
fi

# ── 활성 venv 경고 ──────────────────────────────────────────────────────
if [[ -n "${VIRTUAL_ENV:-}" ]]; then
    warn "Python venv 가 활성 상태입니다 (${VIRTUAL_ENV})"
    warn "ESP-IDF install.sh 가 자체 venv 를 만드므로 충돌 방지를 위해"
    warn "  ${C_BOLD}deactivate${C_RESET}${C_YELLOW} 후 재실행을 권장합니다."
    read -r -p "그래도 계속할까요? [y/N] " yn
    [[ "${yn}" =~ ^[Yy]$ ]] || exit 0
fi


step "1/3  APT 패키지 설치"
${SUDO} apt-get update -qq
${SUDO} apt-get install -y \
    git wget curl flex bison gperf cmake ninja-build ccache \
    libffi-dev libssl-dev dfu-util libusb-1.0-0 \
    python3 python3-pip python3-venv \
    build-essential pkg-config
ok "기본 패키지 설치 완료"


step "2/3  ESP-IDF (${IDF_BRANCH}) 설치 → ${IDF_DIR}"
mkdir -p "$(dirname "${IDF_DIR}")"
if [[ -d "${IDF_DIR}/.git" ]]; then
    ok "${IDF_DIR} 이미 존재 — 클론 건너뜀"
else
    git clone -b "${IDF_BRANCH}" --recursive https://github.com/espressif/esp-idf.git "${IDF_DIR}"
    ok "ESP-IDF 클론 완료"
fi
(cd "${IDF_DIR}" && ./install.sh esp32s3)
ok "ESP-IDF 툴체인 설치 완료"


step "3/3  설치 검증"
# shellcheck disable=SC1091
source "${IDF_DIR}/export.sh" >/dev/null
idf.py --version
ok "idf.py 호출 성공"


cat <<EOF

${C_BOLD}${C_GREEN}═══════════════════════════════════════════════════════════════
  빌드 환경 설치 완료.
═══════════════════════════════════════════════════════════════${C_RESET}

  ${C_BOLD}매 새 터미널마다 ESP-IDF 활성화:${C_RESET}
     source ${IDF_DIR}/export.sh

  ${C_BOLD}빌드:${C_RESET}
     cd ${PROJECT_DIR}
     idf.py set-target esp32s3      # 최초 1회만
     idf.py build

  ${C_BOLD}편의 alias (~/.bashrc 에 추가 가능):${C_RESET}
     alias get_idf='source ${IDF_DIR}/export.sh'

  ${C_BOLD}${C_YELLOW}참고:${C_RESET}
     이 컨테이너에서는 ${C_BOLD}펌웨어 빌드만${C_RESET} 가능합니다.
     • idf.py flash  → USB 패스스루 설정 필요 (현재 불가)
     • python bridge.py → 호스트 NUC 에서 실행 (setup_ubuntu.sh 사용)

EOF
