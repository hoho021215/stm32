# ex1.ioc 검토와 현재 작업 상태

업로드한 [`ex1.ioc`](ex1.ioc)를 프로젝트 기준 파일로 보존합니다. 원본의 ADC·TIM 설정은 이번 작업에서 변경하지 않았습니다. 아래 값은 저장된 설정을 읽어 확인한 것이며 CubeMX에서 코드를 실제 생성해 검증한 결과는 아닙니다.

- 원본 크기: 9535 bytes
- 원본 SHA-256: `e97fede057d79c63cae36afb8ff8fd3d85cce6c9351ebb20e224f8dc8655fb1c`
- 저장된 프로젝트명/파일명: `ex1` / `ex1.ioc`
- CubeMX `6.18.1`, STM32Cube FW_F3 `V1.11.6`

## 그대로 사용할 수 있는 기본 구조

| 항목 | 업로드 파일의 기록 |
|---|---|
| 보드/MCU | NUCLEO-F303ZE / STM32F303ZET6, LQFP144 |
| IDE | STM32CubeIDE / GCC |
| 사용자 코드 보존 | `ProjectManager.KeepUserCode=true` |
| 생성 구조 | `Core/Src`, `CoupleFile=false`: 주변장치 핸들·초기화가 main.c에 모이는 방식 |
| 클럭 | 외부 8MHz HSE 입력, PLL ×9를 사용하는 72MHz 구성 |
| 외부 클럭 핀 | PF0, ST-LINK MCO로 표시됨 |
| USB | PLL /1.5 → 48MHz |
| 기본 기능 | SWD/SWO, LED, 사용자 버튼, USART3, USB, RTC, DAC1 등 |

72MHz 기준은 유지합니다. 이전 예제의 64MHz를 맞추려고 클럭을 바꾸면 현재 USB 48MHz 경로도 다시 설정해야 합니다. ST-LINK MCO 공급과 보드 연결 상태는 실제 보드에서 확인합니다. `PCC.Vdd=3.6`은 전력 계산기 메타데이터이므로 실제 ADC 기준전압으로 사용하지 않습니다.

## ADC·TIM 설정을 다시 받을 때 확인할 차이

| 항목 | 현재 ex1.ioc | 제어 모듈의 임시 목표 |
|---|---|---|
| TIM1 모드 | Center-aligned 1 | Up, edge-aligned |
| TIM1 ARR | 3599 | 1439 |
| PWM 주파수 | PSC 기본값 0 가정 시 약 10kHz | 72MHz /1440 = 명목 50kHz |
| PWM 출력 | PC0 / TIM1_CH1, PA7 / TIM1_CH1N | 같은 핀 쌍 사용 예정 |
| CH4/ADC TRGO | CH4·master trigger의 명시 설정 기록 없음 | CH4 PWM2, CCR4=360, OC4REF → ADC |
| 데드타임 | 명시 설정 기록 없음 | DTG=22, 약 305.6ns 초깃값 |
| ADC 채널 | PA0 / ADC1_IN1 한 채널 | IL·Vout·Vin 3채널 |
| ADC 커널 클럭 | 동기 HCLK /4 → 명목 18MHz | 임시 목표는 PLL 72MHz, async DIV1 |
| ADC sampling | 1.5cycles | 임시 목표 19.5cycles |
| ADC 트리거 | TIM1_TRGO 소스 선택 기록 | 상승 에지와 timer master 신호까지 함께 확인 |
| ADC DMA | 설정 기록 없음 | DMA1 Channel1, circular, 6 halfwords |
| 드라이버 EN | 전용 PC6 설정 없음 | PC6 전용 DRV_EN은 미확정 제안 |

PC0와 PA7은 유효한 타이머 핀입니다. **PC0=AF2 TIM1_CH1, PA7=AF6 TIM1_CH1N**이므로 핀이 잘못 지정된 것은 아닙니다. 이전의 PE9/PE8 제안과 달라졌으므로 실제 J7·게이트 드라이버 배선을 이 기준과 맞춰야 합니다.

센터 정렬은 0→ARR→0으로 왕복하므로 `ARR=3599`, PSC=0 가정에서 `72MHz/(2×3599)≈10.003kHz`입니다. 업로드 파일에 PSC 값은 명시되어 있지 않으므로 최종 생성 코드의 Prescaler도 확인합니다. 업로드 파일의 ADC·TIM을 이 임시 목표로 바꾸지는 않았습니다.

## 지금 주의할 점

1. 저장된 `ProjectManager.functionlistsort`에 `MX_ADC1_Init()`가 없습니다. ADC IP 자체는 있으므로 실제 Generate Code 뒤 함수 생성·호출 유무와 순서를 확인해야 합니다. 저장 목록만으로 생성 실패를 단정하지 않습니다.
2. `NVIC.ForceEnableDMAVector=true`는 ADC DMA 채널 설정을 뜻하지 않습니다. DMA IP/채널 설정 기록이 없습니다.
3. TIM1 break 인터럽트가 켜져 있다는 사실만으로 BKIN 또는 과전류 보호가 구성된 것은 아닙니다. deadtime·break·트리거 극성과 배선을 별도로 확인합니다.
4. `Buck_Init()`는 임시 목표와 맞지 않거나 DMA 핸들이 없으면 **F_SETUP(512)**로 중단하고 ADC DMA를 시작하지 않습니다. 현재 ex1.ioc는 완성된 벅 제어 설정이 아니며 바로 구동할 수 없습니다.
5. `Core/Inc/buck_board.h`의 `BUCK_BOARD_VERIFIED=0`을 유지합니다. ADC·TIM 설정과 전용 EN 배선이 확인되기 전에는 1로 바꾸지 않습니다.

## 다음 업로드에 반영할 내용

ADC·TIM 설정을 마친 새 `.ioc`를 같은 방식으로 받으면 실제 채널·Rank·클럭·PWM 모드·주파수·TRGO·DMA·deadtime에 맞춰 `buck_board.h`, 제어 주기와 전처리 검사를 함께 갱신합니다. 현재 코드의 50kHz edge-aligned 수치는 임시 목표이며 센터 정렬을 유지하는 새 설정을 그대로 지원한다는 뜻이 아닙니다.

L1의 실제 인덕턴스, 출력 총 C/ESR, Q1·Q2 품번도 아직 확정되지 않아 제어 이득·전류 임계값·deadtime은 최종 튜닝값이 아닙니다.

## 검증 범위와 자료

원본 설정의 텍스트 검토와 공식 HAL 헤더를 이용한 모듈 문법 검사를 수행했습니다. 호스트 MMIO 모의 검사 5건에서는 현재 미완성 구성, NULL DMA 핸들, 잘못된 타이머 모드, 일반 DMA 모드를 ADC 시작 전에 거부하고, 임시 목표 구성이 맞는 경우에도 MOE OFF를 유지하는 것을 확인했습니다. 이는 제어 안정성이나 실제 주변장치 동작 시험이 아닙니다.

CubeMX GUI로 열기/Generate Code, 실제 재생성 보존, ARM 대상 빌드·링크, 보드 구동은 수행하지 않았습니다.

- [ST STM32F303xD/E 데이터시트](https://www.st.com/resource/en/datasheet/stm32f303ze.pdf): 핀 대체 기능.
- [ST RM0316](https://www.st.com/resource/en/reference_manual/DM00043574.pdf): 센터 정렬 타이머와 ADC 클럭.
