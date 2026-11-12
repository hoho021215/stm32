# CubeMX 설정과 프로젝트 통합

현재 기준 파일은 업로드한 **[ex1.ioc](ex1.ioc)**이며, 기록된 설정은 **[IOC_REVIEW.md](IOC_REVIEW.md)**를 먼저 확인합니다. 원본의 ADC·TIM 설정은 이번에 변경하지 않았습니다. 아래 표는 72MHz 기반 제어 모듈의 **추후 설정용 임시 목표**이며, 현재 ex1.ioc에 이미 적용된 값이 아닙니다. 사용자가 ADC·TIM 설정을 마친 파일을 올리면 그 실제 값에 맞춰 다시 조정합니다.

## 기준 클럭: 업로드 파일의 72MHz 유지

| 항목 | 업로드 파일 기준 |
|---|---|
| 외부 클럭 | PF0의 HSE 외부 입력, 명목 8MHz ST-LINK MCO |
| PLL | ×9 |
| SYSCLK / HCLK | 72MHz |
| APB1 | HCLK /2 = 36MHz |
| APB2 | HCLK /1 = 72MHz |
| TIM1 | 명목 72MHz |
| USB | PLL /1.5 = 48MHz |

실제 보드에서 ST-LINK MCO 공급·보드 연결을 확인합니다. 이전 제안의 64MHz HSI 구성으로 바꾸지 않습니다. 64MHz로 바꾸면 현재 USB 48MHz 경로도 다시 설계해야 합니다.

ADC는 현재 `.ioc`의 `ADC_CLOCK_SYNC_PCLK_DIV4` 때문에 커널이 명목 18MHz입니다. 아래 ADC 표의 async 72MHz는 아직 적용하지 않은 임시 목표입니다. `RCC.ADC12outputFreq_Value=72000000`만 보고 현재 ADC 변환 클럭도 72MHz라고 해석하지 않습니다.

## 핀: 기존 PWM 핀 유지, 나머지는 미확정 제안

| MCU 핀 | 기능 | 연결 |
|---|---|---|
| PC0 / AF2 | TIM1_CH1 | 현재 ex1.ioc의 PWM_H 핀 → UCC27714 HI |
| PA7 / AF6 | TIM1_CH1N | 현재 ex1.ioc의 PWM_L 핀 → UCC27714 LI |
| PC6 | GPIO Output Push-Pull | **미확정 제안**: 전용 DRV_EN → UCC27714 EN |
| PA0 | ADC1_IN1 | 현재 한 채널 존재. 인덕터 전류 `I_sen` 연결은 배선 확인 필요 |
| PA1 | ADC1_IN2 | **미확정 제안**: 출력전압 `V_sen` |
| PA2 | ADC1_IN3 | **미확정 제안**: 입력전압 `Vin_sen` |

PC6를 전용 EN으로 채택하면 출력 초기값 LOW, GPIO Pull-down을 설정하고 외부 약 10kΩ 풀다운을 추가합니다. MCU 리셋 중에도 드라이버가 OFF여야 합니다. 센서 FAULT와 공유된 선을 HIGH로 강제 구동하면 안 됩니다. FAULT/EN이 동일 PCB net인지 원본 회로도에서 확인하고 필요하면 분리하거나 별도 논리를 구성합니다.

PA0·PA1·PA2는 Analog, No pull로 설정합니다. ADC 입력은 VDDA 범위를 넘어서는 안 됩니다.

## TIM1: 50kHz edge-aligned 임시 목표

| 항목 | 설정 |
|---|---|
| Counter mode | Up |
| Prescaler | 0 |
| Period / ARR | 1439 |
| Clock division | DIV1 |
| Repetition counter | 0 |
| Auto-reload preload | Enable |
| CH1 | PWM Generation CH1/CH1N, PWM mode 1 |
| CH1 Pulse | 0 |
| CH1·CH1N Polarity | HIGH |
| CH1·CH1N Idle State | RESET / LOW |
| CH1 Fast mode | Disable |
| CH1 Compare preload | Enable |
| CH4 | PWM mode 2 |
| CH4 Pulse | 360 |
| Master output trigger / TRGO | **OC4REF** |
| TRGO2 | Reset |
| Master/slave mode | Disable |
| DeadTime / DTG | **22** |
| OSSI / Off-state idle | Enable |
| OSSR / Off-state run | Enable |
| Automatic output | Disable |
| Break / Break2 | Disable: 현 예제에는 비교기/BKIN 배선 미정 |

현재 ex1.ioc는 센터 정렬·ARR=3599로 약 10kHz(PSC=0 가정)입니다. 이 표의 임시 목표는 **Up 모드**에서 `1440/72MHz=20µs`, 명목 50kHz입니다. DTG=22는 약 305.6ns이며 실제 MOSFET·게이트 저항·배선을 기준으로 검증하고 조정해야 합니다. 데드타임을 변경하면 `buck_board.h`의 `BUCK_DEADTIME_TICKS`도 함께 맞춥니다. `Buck_Init()`는 이 값을 참조해 검사합니다.

CH4는 외부 게이트 구동이 아니라 내부 ADC 트리거용입니다. CubeMX가 CH4 외부 핀을 배정하더라도 보드 회로에는 연결하지 않습니다. 코드가 CH4의 외부 출력을 시작하지 않습니다. PWM2의 OC4REF가 카운터 360, 즉 주기 시작 후 약 5µs에 상승해 ADC를 트리거합니다.

고정 5µs 시점은 듀티 25% 부근에서 스위칭 에지와 겹칠 수 있습니다. 실제 노이즈·센서 위상 지연을 측정해야 하며 표본값을 정확한 평균 또는 피크 전류로 해석하지 않습니다.

## ADC1과 DMA: 추후 추가할 임시 목표

| 항목 | 설정 |
|---|---|
| Resolution / Data alignment | 12bit / Right |
| Scan conversion mode | Enable |
| Number of conversions | 3 |
| Rank 1 | ADC1_IN1 / PA0 / IL |
| Rank 2 | ADC1_IN2 / PA1 / Vout |
| Rank 3 | ADC1_IN3 / PA2 / Vin |
| Sampling time | 각 채널 19.5cycles |
| Single/Differential | Single-ended |
| Clock prescaler | Asynchronous DIV1 |
| EOC selection | End of sequence |
| Low-power Auto Wait | Disable |
| Continuous conversion | Disable |
| Discontinuous conversion | Disable |
| External trigger | TIM1_TRGO |
| Trigger edge | Rising |
| DMA continuous requests | Enable |
| Overrun | Data preserved |
| DMA | DMA1 Channel1 / Peripheral to memory |
| DMA mode | Circular |
| Peripheral increment | Disable |
| Memory increment | Enable |
| Peripheral / memory alignment | Halfword / Halfword |
| DMA priority | Very high |

현재 ex1.ioc에는 PA0 한 채널만 있고 DMA 설정 기록이 없습니다. 위 표의 세 채널·트리거·DMA 설정이 갖춰져야 모듈이 `HAL_ADC_Start_DMA(..., 6)`으로 `[IL,Vout,Vin]` 두 프레임을 시작합니다. Half-transfer와 Transfer-complete 콜백을 모두 사용하므로 둘 중 하나를 끄면 안 됩니다. `adc_frame[6]`은 DMA가 접근할 수 있는 **일반 SRAM**에 배치하고 CCM RAM에는 두지 않습니다.

## 추후 설정 후 인터럽트와 빌드

- DMA1 Channel1과 ADC1_2 글로벌 인터럽트를 Enable, 우선순위 0으로 설정합니다. CubeMX가 생성하는 IRQ handler에서 각 HAL handler 호출을 유지합니다.
- SysTick과 HAL tick을 유지합니다. `Buck_Poll()`이 ADC 프레임 갱신 정지를 감시합니다.
- 타깃 Cortex-M4, FPU `fpv4-sp-d16`, 프로젝트의 HAL·라이브러리와 일치하는 hard-float ABI를 사용합니다.
- 최적화 `-O2`, `-ffast-math` 사용 금지. 콜백 안에 UART 출력·printf·지연·블로킹 함수를 추가하지 않습니다.

## Code Generator와 사용자 코드 통합

Toolchain/IDE는 **STM32CubeIDE**로 선택하고 Project Manager → Code Generator → **Keep User Code when re-generating**을 켭니다. 주변장치 초기화를 별도 `.c/.h` 파일로 생성하는 옵션은 켜거나 꺼도 되며, 생성된 핸들·초기화 함수와 헤더를 유지합니다. `hadc1`, `htim1`을 사용자 파일에서 다시 정의하지 않습니다. 현재 원본은 `CoupleFile=false`이며 기존 main.c의 초기화 호출을 유지합니다. 저장 목록에 MX_ADC1_Init()가 없으므로 Generate Code 결과에서 호출 유무를 확인하고, DMA 추가 후 초기화 순서도 확인합니다.

실제 삽입 위치는 **[CUBEMX_USER_CODE.md](CUBEMX_USER_CODE.md)**를 따릅니다.

| 생성 파일 | 기존 USER CODE 구역 | 직접 추가할 내용 |
|---|---|---|
| `main.c` | `Includes` | `#include "buck_app.h"` |
| `main.c` | `2` | 모든 생성된 `MX_*_Init()` 뒤 `Buck_Init();` |
| `main.c` | `3` | 기존 반복문 안 `Buck_Poll();` |
| `main.c` | `Error_Handler_Debug` | 인터럽트를 끈 뒤, 기존 정지 루프 앞 `Buck_EmergencyOff();` |
| `stm32f3xx_it.c` | `Includes` | `#include "buck_app.h"` |
| `stm32f3xx_it.c` | `HardFault_IRQn 0` | 기존 정지 루프 앞 `Buck_EmergencyOff();` |

`main.h`에 함수 원형을 직접 적을 필요는 없습니다. `buck_app.h`를 include하면 됩니다. ADC 콜백은 `buck_app.c`가 소유하므로 `main.c`의 `USER CODE 4`에 다시 만들지 않습니다. `HAL_TIM_PWM_Start()`나 `HAL_TIMEx_PWMN_Start()`도 추가 호출하지 않습니다.

`integration/*.example`은 생성된 원본에 사용자 코드를 넣을 위치를 보여 주는 참고 자료입니다. 전체 파일을 대체하거나 빌드하지 않습니다. 기존 생성된 HAL IRQ 처리와 SysTick 호출은 그대로 둡니다. CubeIDE에서 파일 Refresh, 활성 빌드의 Exclude from Build 해제 및 `Core/Inc` include path를 확인합니다.

## 최초 확인

**현재 ex1.ioc로는 설정 오류 F_SETUP(512)로 중단할 수 있습니다. ADC·TIM·DMA·EN 설정이 완료된 이후** `buck_board.h`의 `BUCK_BOARD_VERIFIED=0`을 유지한 상태에서 전류가 흐르지 않도록 하고 센서 전원을 공급합니다. 2048프레임, 약 40.96ms 후 `buck_debug.calibrated=1`인지 확인합니다. 전압은 DMM과 비교해 `BUCK_ADC_VDDA_V` 및 분압비를 확인하고, 전류 방향에 맞춰 `BUCK_CURRENT_POLARITY`를 설정합니다.

전력 입력이 제거된 별도 시험 조건에서 상·하단 게이트 파형과 데드타임을 검증합니다. 기본값은 기동을 막으므로 해당 PWM 파형 시험은 별도 시험 절차가 필요합니다. 전용 EN 배선·보호·소자 정격·센싱을 확인한 뒤 `BUCK_BOARD_VERIFIED=1`로 변경합니다. 영점 보정 완료 후 디버거에서 `buck_arm_request=1`로 기동하며 정지는 2, 고장 해제는 3입니다.

기충전 출력은 기본적으로 기동을 거부합니다. 코드의 1.8A 차단값 등은 실제 인덕터 리플·센서 지연·소자 정격으로 재검토해야 합니다. 이 설정만으로 제어 안정성이나 보호 동작이 검증되는 것은 아닙니다.
