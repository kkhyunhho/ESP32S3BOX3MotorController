# Ubuntu (NUC) 개발 환경 세팅 가이드

새 우분투 머신에서 ESP32-S3-BOX-3 + MKS SERVO57D 브리지를 빌드/실행하기
위해 필요한 의존성과 1회성 설정을 정리한 문서입니다.

대상 OS: Ubuntu 22.04 LTS 이상 (24.04 권장).

---

## ⚡ 빠른 경로 — 한 줄 설치

아래 1~6장을 한 번에 자동화한 스크립트가 있습니다. 보통은 이걸로 충분합니다.

```bash
bash setup_ubuntu.sh
```

수행 내용:

1. APT 기본 패키지 설치
2. ESP-IDF (release/v5.3) 클론 + `install.sh esp32s3`
3. 프로젝트 루트에 `.venv` 생성 후 `pyserial`, `ftd2xx` 설치
4. `libftd2xx` 1.4.27 다운로드 + `/usr/local/lib` 설치
5. `lsusb` 로 FTDI PID 자동 탐지 → udev 규칙 작성 (`ftdi_sio` 바인딩 해제)
6. 사용자를 `dialout` 그룹에 추가
7. `bridge.py` 의 `ESP32_PORT = 'COM6'` → `'/dev/ttyACM0'` 로 자동 치환
   (백업: `bridge.py.bak`)
8. 설치 검증 + 다음 단계 안내 출력

스크립트 종료 후:

- **로그아웃 → 재로그인** (dialout 그룹 적용)
- `source ~/esp/esp-idf/export.sh` 로 ESP-IDF 활성화
- `source .venv/bin/activate && python bridge.py`

스크립트가 막히거나 수동으로 단계별로 확인하고 싶으면 아래 본문 참조.

---

## 0. 준비 — 시스템 패키지

ESP-IDF 빌드, Python, libusb, FTDI 빌드/링크에 필요한 기본 패키지를 먼저
설치합니다.

```bash
sudo apt update
sudo apt install -y \
    git wget flex bison gperf cmake ninja-build ccache \
    libffi-dev libssl-dev dfu-util libusb-1.0-0 \
    python3 python3-pip python3-venv \
    build-essential pkg-config
```

> `python3 --version` 으로 3.10 이상인지 확인하세요. ESP-IDF v5.3+ 는
> 3.9 이상이면 됩니다.

---

## 1. ESP-IDF 설치 (펌웨어 빌드용)

ESP32-S3-BOX-3 BSP는 ESP-IDF **v5.3 이상**(현 프로젝트 타깃 v6.0.1)을
요구합니다.

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b release/v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

매 터미널마다 환경을 활성화합니다 (또는 `~/.bashrc` 에 alias 등록):

```bash
. ~/esp/esp-idf/export.sh
# 또는
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

확인:

```bash
idf.py --version          # ESP-IDF v5.3.x
idf.py set-target esp32s3 # 프로젝트 루트에서 1회 실행
idf.py build
```

ESP32 보드에 플래시할 때는 USB-C **데이터 포트**를 사용해야 합니다.

```bash
idf.py -p /dev/ttyACM0 flash monitor   # COM<N> 대신 /dev/ttyACM* 사용
```

> `idf.py monitor` 와 `bridge.py` 는 같은 시리얼 포트를 점유합니다.
> 브리지 실행 전에 모니터를 반드시 종료하세요.

---

## 2. Python 패키지

프로젝트 루트에서 가상환경 1개를 두고 쓰는 걸 추천합니다.

```bash
cd ~/path/to/ESP32S3BOX3MotorController
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install pyserial ftd2xx
```

각 라이브러리 용도:

| 패키지 | 사용처 | 비고 |
|--------|--------|------|
| `pyserial` | `bridge.py` 에서 ESP32 USB 시리얼 읽기 | `import serial` |
| `ftd2xx` | `mks_motor.py` 에서 USB2CAN(FTDI FT245) 제어 | **D2XX 커널 드라이버 필요(아래 3장)** |

설치 검증:

```bash
python -c "import serial; print(serial.__version__)"
python -c "import ftd2xx; print(ftd2xx.__version__)"
```

---

## 3. FTDI D2XX 드라이버 + 커널 모듈 충돌 해결 ⚠️

`ftd2xx` Python 패키지는 FTDI 의 독점 **D2XX** 사용자 공간 라이브러리
(`libftd2xx.so`)를 호출합니다. 그런데 우분투는 FTDI 장치가 꽂히면 자동으로
오픈소스 커널 모듈 `ftdi_sio` 가 디바이스를 `/dev/ttyUSB*` 로 잡아버려
D2XX 가 디바이스를 열 수 없게 됩니다. **이 부분이 리눅스 이전 시 가장 자주
막히는 지점입니다.**

### 3-1. libftd2xx 설치

[FTDI 공식 페이지](https://ftdichip.com/drivers/d2xx-drivers/) 에서 리눅스
x86_64 tarball 을 받아 시스템에 설치합니다.

```bash
cd /tmp
wget https://ftdichip.com/wp-content/uploads/2022/07/libftd2xx-x86_64-1.4.27.tgz
tar xf libftd2xx-x86_64-1.4.27.tgz
cd release
sudo cp build/libftd2xx.so.1.4.27 /usr/local/lib
sudo ln -sf /usr/local/lib/libftd2xx.so.1.4.27 /usr/local/lib/libftd2xx.so
sudo ldconfig
```

> 버전 번호(`1.4.27`)는 다운로드 시점에 맞게 바꿔주세요.

### 3-2. ftdi_sio 자동 바인딩 끄기 (udev 규칙)

USB2CAN 어댑터 3개에 대해서만 `ftdi_sio` 가 디바이스를 잡지 못하도록
udev 규칙을 추가합니다. ESP32-S3-BOX-3 는 CDC-ACM 으로 잡혀
`/dev/ttyACM*` 로 뜨므로 영향 없음.

```bash
sudo tee /etc/udev/rules.d/99-ftdi-usb2can.rules >/dev/null <<'EOF'
# USB2CAN (FTDI FT245) — D2XX 가 직접 열도록 ftdi_sio/usbserial 바인딩 제거
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", \
    MODE="0666", \
    RUN+="/bin/sh -c 'echo $kernel > /sys/bus/usb/drivers/ftdi_sio/unbind 2>/dev/null; \
                      echo $kernel > /sys/bus/usb/drivers/usbserial/unbind 2>/dev/null'"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

> `idProduct` 가 `6014` (FT232H), `6010` (FT2232H) 등 다른 칩이면 해당
> PID 로 바꾸세요. `lsusb` 출력으로 확인:
>
> ```bash
> lsusb | grep -i ftdi
> # Bus 001 Device 010: ID 0403:6001 Future Technology Devices ...
> ```

USB2CAN 어댑터를 뽑았다 다시 꽂은 뒤 아래 명령이 빈 줄이어야 정상입니다
(`ftdi_sio` 가 안 잡혀야 함):

```bash
dmesg | tail | grep ftdi_sio
```

### 3-3. 시리얼 그룹 권한

ESP32 (`/dev/ttyACM0`) 접근을 위해 로그인 사용자를 `dialout` 그룹에
추가합니다. 추가 후 한 번 로그아웃/재로그인 필요.

```bash
sudo usermod -a -G dialout $USER
# 로그아웃 → 재로그인 또는 재부팅
groups | grep dialout
```

---

## 4. `bridge.py` 수정 — 시리얼 포트 경로

윈도우 `COM6` 은 리눅스에 존재하지 않습니다. ESP32 를 꽂은 뒤 실제 경로를
확인해 [bridge.py:15](bridge.py#L15) 의 `ESP32_PORT` 를 수정하세요.

```bash
ls /dev/ttyACM* /dev/ttyUSB*   # 어떤 노드로 뜨는지 확인
dmesg | tail                   # 방금 꽂은 장치 확인 가능
```

대부분 BOX-3 는 `/dev/ttyACM0` 으로 잡힙니다.

```python
# bridge.py
ESP32_PORT = '/dev/ttyACM0'    # 기존 'COM6' 에서 변경
```

> 포트 이름이 매번 바뀌면 udev 규칙으로 고정된 심볼릭 링크
> (예: `/dev/esp32-box3`)를 만드는 걸 추천하지만, 한 보드만 꽂으면 보통
> `/dev/ttyACM0` 으로 일관됩니다.

---

## 5. 실행 점검 체크리스트

새 환경에서 한 번 끝까지 따라 도는지 확인:

- [ ] `idf.py build` 가 경고 없이 끝남
- [ ] `idf.py -p /dev/ttyACM0 flash monitor` 로 부팅 로그 + 백라이트 켜짐 확인
- [ ] `python CAN2USBAdapterDeviceRecognition.py` 가 USB2CAN 3개를
      Port 0/1/2 로 나열함
- [ ] `bridge.py` 의 `PORT_Z_A / PORT_Z_B / PORT_X` 가 위 결과와 일치
- [ ] `idf.py monitor` 종료 후 `python bridge.py` 실행 → "Bridge ready"
      메시지
- [ ] 터치 디스플레이 Z+/Z-/X+/X-/HOME 동작 확인

---

## 6. 자주 막히는 지점

| 증상 | 원인 / 해결 |
|------|-------------|
| `ftd2xx.ftd2xx.DeviceError: DEVICE_NOT_OPENED` | `ftdi_sio` 가 디바이스를 선점. 3-2 udev 규칙 적용 + 어댑터 재삽입 |
| `ImportError: libftd2xx.so: cannot open shared object file` | 3-1 의 `ldconfig` 누락. `/usr/local/lib` 에 라이브러리가 있는지 확인 |
| `serial.serialutil.SerialException: [Errno 13] Permission denied: '/dev/ttyACM0'` | `dialout` 그룹 미가입. 3-3 후 재로그인 |
| `idf.py: command not found` | `. ~/esp/esp-idf/export.sh` 안 함 |
| 어댑터 포트 번호가 매번 바뀜 | USB 허브 순서 영향. 매 실행 전 `CAN2USBAdapterDeviceRecognition.py` 로 확인 후 `bridge.py` 갱신 |

---

## 7. 옵션 — 한 줄 실행 헬퍼

윈도우의 [launch_bridge.bat](launch_bridge.bat) 대응으로, 리눅스에서는
다음 셸 스크립트를 프로젝트 루트에 두면 편합니다 (필요 시 별도 생성).

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
source .venv/bin/activate
python bridge.py
```
