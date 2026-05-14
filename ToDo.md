# ToDo

작업 단위 추적 파일. **append-only** — 과거 항목은 삭제하지 않고
체크박스만 갱신한 뒤 한 줄 요약을 덧붙인다.

형식:

```markdown
## YYYY-MM-DD | 작업 제목
- [ ] 서브태스크 1
- [ ] 서브태스크 2
> 완료 후 한 줄 요약
```

---

## 2026-05-14 | 프로젝트 정리 (불필요 파일 제거 / README 재작성)
- [x] `pytest_hello_world.py` 삭제 (hello_world 템플릿 잔재)
- [x] `sdkconfig.ci` 삭제 (0 바이트 CI 설정 잔재)
- [x] `sdkconfig.old` 삭제 (menuconfig 자동 백업)
- [x] `__pycache__/` 삭제 (Python 바이트코드 캐시)
- [x] `README.md` 재작성 — bridge 모드 아키텍처 반영
- [x] `ToDo.md` 생성

> 정리 완료. 다음 작업부터 이 파일에 항목을 누적해 나간다.

## 2026-05-14 | CLAUDE.md 갱신 (bridge 모드 / Y축 예정 반영)
- [x] Project 섹션 — 모터 4개(CAN) → 모터 3개(시리얼 브리지), Y축은 planned 명시
- [x] System architecture 다이어그램 추가
- [x] File layout — `bridge.py` / `mks_motor.py` 추가, `can_motor.c`는 legacy 명시
- [x] Initialization order — CAN 초기화 / rx_drain_task 제거
- [x] Serial command protocol 표 추가
- [x] MKS CAN protocol 섹션을 PC 측(`mks_motor.py`) 기준으로 재배치
- [x] Hardware 섹션 — ESP32 측 CAN 배선/트랜시버 설명 제거, 종단 저항만 유지
- [x] Python 코드 컨벤션 섹션 추가
- [x] "Planned: Y-axis addition" 체크리스트 추가 (양쪽 동시 변경 가이드)
- [x] Known traps — CAN 관련 항목 정리, 브리지/Y축 트랩 추가

> Y축 추가 시 변경 지점 5곳을 CLAUDE.md "Planned" 섹션에 박제. 다음 작업부터 그 체크리스트대로 진행.

## 2026-05-14 | README.md에 USB2CAN 포트 확인 절차 추가
- [x] "PC 브리지 실행" 섹션에 `CAN2USBAdapterDeviceRecognition.py` 실행 절차 추가
- [x] 포트 번호 매번 바뀔 수 있다는 경고 명시
- [x] 폴더 구조 표에 해당 스크립트 포함
- [x] 트러블슈팅에 "엉뚱한 축이 움직임" 케이스 추가

> 브리지 실행 전 포트 매핑 확인을 필수 절차로 못박음.

## 2026-05-14 | README.md 전체 영문화
- [x] 한국어 본문을 영어로 전면 재작성 (구조/내용은 유지)

> README는 영어로 유지. ToDo.md / 대화는 계속 한국어.
