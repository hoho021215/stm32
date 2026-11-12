# STM32F303ZE 동기식 벅 컨버터 제어 예제

NUCLEO-F303ZE와 UCC27714를 사용하는 **입력 30V → 출력 15V·1A, 스위칭 50kHz** 벅 컨버터용 STM32CubeF3 HAL 모듈입니다. CubeMX가 생성한 프로젝트에 추가해서 사용합니다. 완성된 CubeIDE 프로젝트나 플래시용 바이너리는 포함하지 않습니다.

## 먼저 적용할 순서

1. [CUBEMX_SETUP.md](CUBEMX_SETUP.md)에 따라 `.ioc`에서 핀·클럭·ADC·PWM을 설정하고 Generate Code를 실행합니다.
2. [`Core/Src/buck_app.c`](Core/Src/buck_app.c), [`Core/Inc/buck_app.h`](Core/Inc/buck_app.h)를 같은 프로젝트 경로에 직접 추가합니다.
3. **[CUBEMX_USER_CODE.md](CUBEMX_USER_CODE.md)**를 따라 생성된 `main.c`와 `stm32f3xx_it.c`의 기존 `USER CODE BEGIN/END` 구역 안에 호출만 넣습니다.

직접 넣을 위치는 `Includes`의 헤더, `2`의 `Buck_Init()`, `3`의 `Buck_Poll()`, 오류·HardFault 구역의 비상 차단입니다. 필요한 사용자 추가 부분은 한글 주석으로 표시했습니다. 생성된 초기화 함수와 HAL IRQ 호출은 유지합니다. ADC 콜백은 `buck_app.c`에 있으므로 `main.c`에 중복 구현하지 않습니다.

전체 배치 참고는 [`integration/main.c.example`](integration/main.c.example), [`integration/stm32f3xx_it.c.example`](integration/stm32f3xx_it.c.example)에 있습니다. 이 파일들은 위치 설명용 부분 예제이며 기존 생성 파일을 덮어쓰거나 빌드하는 용도가 아닙니다. 재생성 전 **Keep User Code when re-generating**을 켭니다.

## 회로와 센싱 기준

| 항목 | 기준 |
|---|---|
| MCU | STM32F303ZETx / NUCLEO-F303ZE |
| 클럭 | HSI 8MHz / PREDIV 2 × PLL 16 = HCLK·TIM1 64MHz |
| 드라이버 | UCC27714, 전용 EN 제어 |
| 전류센서 | ACS71240LLCBTR-010B3, 인덕터 전류 측정 |
| 센서 환산 | 3.3V 전원, 명목 영점 1.65V, 감도 0.132V/A |
| 전압 검출 | 상단 27kΩ·하단 1.5kΩ, 분압비 19 |
| MCP6002 | 입력·출력 전압 검출용 단위 이득 버퍼 |

전압은 `V = ADC × VDDA / 4095 × 19`, 전류는 `IL = (ADC × VDDA / 4095 − 보정 영점) / 0.132`로 환산합니다. `VDDA`와 `CURRENT_POLARITY`는 실측에 맞춰 확인해야 합니다.

## 포함 기능

- 입력전압 피드포워드 `Dff = Vref / Vin`.
- 300ms 기준전압 램프와 듀티 상승 제한 1.0/s. 실제 출력의 300ms 정착을 보장하지 않으며, 듀티 0.5 도달에는 최소 약 500ms가 필요합니다.
- 10kHz 전압 피드백, 50kHz 센싱·피드포워드·전류 감독.
- 전류 제한까지 반영한 최종 명령 듀티 기준 tracking anti-windup.
- 소프트웨어 전류 duty-cap, 고장 유지, 기충전 출력 기동 거부.
- 양쪽 MOSFET OFF 상태의 2048표본 영점 보정과 약 1ms 저측 부트스트랩 충전.
- DWT를 이용한 콜백 간격·처리 시간 감시 및 ADC 갱신 정지 감시.

전류 duty-cap은 정전류(CC) 루프나 주기별 피크 전류 차단 기능이 아닙니다. 고정 시점의 전류 표본을 정확한 평균·피크 전류로 해석하지 마세요.

## 기동과 디버깅

기본값 `BUCK_BOARD_VERIFIED=0`에서는 ADC·텔레메트리만 동작하고 기동하지 않습니다. 실제 배선, 센싱, 게이트 파형, 데드타임과 보호를 검증한 뒤 1로 바꿉니다. 실패한 시작 요청은 폐기되며 나중에 자동으로 기동하지 않습니다.

디버거의 `buck_arm_request`에 다음 값을 씁니다.

| 명령 | 동작 |
|---:|---|
| 1 | 보정·설정·입력·초기 출력 조건이 맞으면 기동 |
| 2 | 정지. 기존 고장은 유지 |
| 3 | 비활성 상태의 고장 해제. 재기동하지 않음 |

`buck_debug`에서 `vin`, `vout`, `il`, `zero`, `duty`, `calibrated`, `state`, `faults`, `frames`, `max_cycles`, `verified`를 확인합니다. 상태는 `0=DISARMED`, `1=PRECHARGE`, `2=SOFTSTART`, `3=RUN`, `4=FAULT`입니다. 텔레메트리는 디버거 관찰용이며 구조체 전체의 원자적 스냅샷을 제공하지 않습니다.

고장 비트는 합산될 수 있습니다.

| 비트값 | 의미 |
|---:|---|
| 1 | ADC/측정 오류 |
| 2 | 영점 보정 범위·변동 오류 |
| 4 | ADC 처리 시점·간격·갱신 감시 오류 |
| 8 | 입력전압 범위 오류 |
| 16 | 출력 과전압 |
| 32 | 양의 과전류 |
| 64 | 역전류 |
| 128 | 초기 출력 기충전 |
| 256 | 기동 후 출력 도달 실패 |
| 512 | 타이머 등 설정 오류 |

## 조정이 필요한 값과 보호 한계

전압 이득 `Kp=0.005`, `Ki=0.25`, anti-windup 계수 `50/s`, 최대 듀티 `0.75`, 전류 제한 `1.15A`/복귀 `1.05A`, 차단 `1.8A`/역방향 `−0.4A`, OVP `17V`, 입력 범위 `23~36V`, 데드타임 약 `297ns`는 **조정용 초깃값**입니다. L1 인덕턴스, 출력 총 커패시턴스·ESR, MOSFET 품번, 부하와 센서 지연을 확인하지 않은 상태에서 안정성이나 최대 전류를 보장하지 않습니다.

UCC27714에는 상·하단 동시 켜짐 방지 기능이 없으므로 TIM1 CH1/CH1N의 하드웨어 데드타임이 필요합니다. 드라이버 전원은 데이터시트의 10~17V 범위로 공급하며 30V를 직접 연결하지 않습니다. 정지는 EN LOW와 MOE 해제를 함께 사용합니다. 듀티 0만 쓰면 상보 저측 MOSFET이 계속 켜질 수 있습니다.

PC6는 외부 약 10kΩ 풀다운을 둔 **전용 DRV_EN**이어야 합니다. PCB의 `GPIO_1`이 ACS71240의 open-drain FAULT와 드라이버 EN을 공유한다면 그대로 push-pull HIGH로 구동하지 말고, 신호 분리 또는 적절한 enable 논리를 먼저 구성해야 합니다.

ACS71240-010B3의 기본 FAULT 기준은 ±10A이므로 이 1A 컨버터의 빠른 단락 보호로 사용할 수 없습니다. 별도 비교기→TIM1 BKIN 등 독립 보호 경로를 검토해야 합니다. ADC 표본 기반 보호는 센서 단선이나 모든 이상 상태를 판별하지 못합니다. 영점 보정 중 외부에서 흐르는 전류가 없어야 합니다.

## 검증 범위

공식 STM32F3 HAL·CMSIS 헤더를 이용한 GCC 문법 검사(`-Wall -Wextra -Werror`)를 통과했습니다. 사용자의 실제 `.ioc`가 없어 CubeMX 재생성 보존 시험은 수행하지 않았습니다. **ARM 대상 빌드·링크와 실제 보드 시험도 수행하지 않았습니다.**

## 참고 자료

- [Allegro ACS71240 데이터시트](https://www.allegromicro.com/-/media/files/datasheets/acs71240-datasheet.pdf)
- [TI UCC27714 데이터시트](https://www.ti.com/lit/ds/symlink/ucc27714.pdf)
- [Microchip MCP6002](https://www.microchip.com/en-us/product/MCP6002)
- [ST STM32F303ZE 데이터시트](https://www.st.com/resource/en/datasheet/stm32f303ze.pdf)
- [ST RM0316 레퍼런스 매뉴얼](https://www.st.com/resource/en/reference_manual/DM00043574.pdf)
