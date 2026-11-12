# CubeMX 설정과 프로젝트 통합

대상은 NUCLEO-F303ZE의 STM32F303ZETx입니다. 핀은 제안한 MCU 연결이며 PCB J7의 핀 순서를 의미하지 않습니다. 이 설정은 `buck_app.c`가 요구하는 값입니다.

## 클럭

| 항목 | 설정 |
|---|---|
| HSI | 8MHz ON |
| PLL 소스 | HSI |
| PREDIV | **DIV2** |
| PLL 배수 | MUL16 |
| SYSCLK / HCLK | 64MHz |
| APB1 | HCLK / 2 = 32MHz |
| APB2 | HCLK / 1 = 64MHz |
| TIM1 클럭 소스 | HCLK = 64MHz |
| ADC12 클럭 소스 | PLLCLK / 1 = 64MHz |

STM32F303xD/E에서는 HSI가 PREDIV를 거칩니다. HSI 8MHz에 DIV1·MUL16을 적용하면 64MHz 설정이 아닙니다. CubeMX Clock Configuration에서 실제 경로를 확인하세요. HSI 오차에 따라 스위칭 주파수에도 오차가 있습니다.

## 핀

| MCU 핀 | 기능 | 연결 |
|---|---|---|
| PE9 / AF2 | TIM1_CH1 | PWM_H → UCC27714 HI |
| PE8 / AF2 | TIM1_CH1N | PWM_L → UCC27714 LI |
| PC6 | GPIO Output Push-Pull | **전용 DRV_EN** → UCC27714 EN |
| PA0 | ADC1_IN1 | 인덕터 전류 `I_sen` |
| PA1 | ADC1_IN2 | 출력전압 `V_sen` |
| PA2 | ADC1_IN3 | 입력전압 `Vin_sen` |

PC6 출력 초기값 LOW, GPIO Pull-down을 설정하고 외부 약 10kΩ 풀다운을 추가합니다. MCU 리셋 중에도 드라이버가 OFF여야 합니다. 센서 FAULT와 공유된 선을 HIGH로 강제 구동하면 안 됩니다. FAULT/EN이 동일 PCB net인지 원본 회로도에서 확인하고 필요하면 분리하거나 별도 논리를 구성합니다.

PA0·PA1·PA2는 Analog, No pull로 설정합니다. ADC 입력은 VDDA 범위를 넘어서는 안 됩니다.

## TIM1

| 항목 | 설정 |
|---|---|
| Counter mode | Up |
| Prescaler | 0 |
| Period / ARR | 1279 |
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
| CH4 Pulse | 320 |
| Master output trigger / TRGO | **OC4REF** |
| TRGO2 | Reset |
| Master/slave mode | Disable |
| DeadTime / DTG | **19** |
| OSSI / Off-state idle | Enable |
| OSSR / Off-state run | Enable |
| Automatic output | Disable |
| Break / Break2 | Disable: 현 예제에는 비교기/BKIN 배선 미정 |

64MHz에서 주기는 `1280/64MHz=20µs`, 주파수는 명목 50kHz입니다. DTG=19는 약 296.875ns이며 실제 MOSFET·게이트 저항·배선을 기준으로 검증하고 조정해야 합니다. 데드타임을 변경하면 `Buck_Init()`의 설정 검사값도 함께 수정합니다.

CH4는 외부 게이트 구동이 아니라 내부 ADC 트리거용입니다. CubeMX가 CH4 외부 핀을 배정하더라도 보드 회로에는 연결하지 않습니다. 코드가 CH4의 외부 출력을 시작하지 않습니다. PWM2의 OC4REF가 카운터 320, 즉 주기 시작 후 약 5µs에 상승해 ADC를 트리거합니다.

고정 5µs 시점은 듀티 25% 부근에서 스위칭 에지와 겹칠 수 있습니다. 실제 노이즈·센서 위상 지연을 측정해야 하며 표본값을 정확한 평균 또는 피크 전류로 해석하지 않습니다.

## ADC1과 DMA

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

모듈이 `HAL_ADC_Start_DMA(..., 6)`으로 `[IL,Vout,Vin]` 두 프레임을 시작합니다. Half-transfer와 Transfer-complete 콜백을 모두 사용하므로 둘 중 하나를 끄면 안 됩니다. `adc_frame[6]`은 DMA가 접근할 수 있는 **일반 SRAM**에 배치하고 CCM RAM에는 두지 않습니다.

## 인터럽트와 빌드

- DMA1 Channel1과 ADC1_2 글로벌 인터럽트를 Enable, 우선순위 0으로 설정합니다. CubeMX가 생성하는 IRQ handler에서 각 HAL handler 호출을 유지합니다.
- SysTick과 HAL tick을 유지합니다. `Buck_Poll()`이 ADC 프레임 갱신 정지를 감시합니다.
- 타깃 Cortex-M4, FPU `fpv4-sp-d16`, 프로젝트의 HAL·라이브러리와 일치하는 hard-float ABI를 사용합니다.
- 최적화 `-O2`, `-ffast-math` 사용 금지. 콜백 안에 UART 출력·printf·지연·블로킹 함수를 추가하지 않습니다.

## 통합

1. `buck_app.c`를 `Core/Src/`, `buck_app.h`를 `Core/Inc/`에 복사합니다.
2. `main.c`의 USER CODE Includes 영역에 `#include "buck_app.h"`를 추가합니다.
3. CubeMX 초기화 뒤 `Buck_Init()`를 한 번 호출합니다.

```c
MX_GPIO_Init();
MX_DMA_Init();
MX_ADC1_Init();
MX_TIM1_Init();

Buck_Init();

while (1)
{
    Buck_Poll();
}
```

`hadc1`, `htim1` 핸들 이름을 유지합니다. `HAL_TIM_PWM_Start()` 또는 `HAL_TIMEx_PWMN_Start()`를 추가로 호출하지 않습니다. 해당 함수는 MOE를 조기에 활성화할 수 있습니다. 모듈이 타이머와 게이트 허용 순서를 관리합니다.

기존 `HAL_ADC_ConvHalfCpltCallback`, `HAL_ADC_ConvCpltCallback`, `HAL_ADC_ErrorCallback`이 있으면 함수 중복을 없애고 로직을 합칩니다. `Error_Handler()`와 `HardFault_Handler()`에서도 정지 루프에 들어가기 전에 `Buck_EmergencyOff()`를 호출합니다. IRQ 파일에서는 헤더를 include해야 합니다. 이 함수는 정상 GPIO·타이머 초기화 이후의 차단 수단이며 하드웨어 독립 보호를 대신하지 않습니다.

## 최초 확인

`BUCK_BOARD_VERIFIED=0`을 유지한 상태에서 전류가 흐르지 않도록 하고 센서 전원을 공급합니다. 2048프레임, 약 40.96ms 후 `buck_debug.calibrated=1`인지 확인합니다. 전압은 DMM과 비교해 `VDDA` 및 분압비를 확인하고, 전류 방향에 맞춰 `CURRENT_POLARITY`를 설정합니다.

전력 입력이 제거된 별도 시험 조건에서 상·하단 게이트 파형과 데드타임을 검증합니다. 기본값은 기동을 막으므로 해당 PWM 파형 시험은 별도 시험 절차가 필요합니다. 전용 EN 배선·보호·소자 정격·센싱을 확인한 뒤 `BUCK_BOARD_VERIFIED=1`로 변경합니다. 영점 보정 완료 후 디버거에서 `buck_arm_request=1`로 기동하며 정지는 2, 고장 해제는 3입니다.

기충전 출력은 기본적으로 기동을 거부합니다. 코드의 1.8A 차단값 등은 실제 인덕터 리플·센서 지연·소자 정격으로 재검토해야 합니다. 이 설정만으로 제어 안정성이나 보호 동작이 검증되는 것은 아닙니다.
