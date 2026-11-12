# CubeMX 생성 파일에서 직접 추가할 코드

이 문서는 **CubeMX가 만든 원본 파일을 유지하면서 사용자 코드만 넣는 방법**입니다. 저장소의 `buck_app.c/.h`는 사용자가 별도로 추가하는 파일이며 CubeMX 자동 생성 파일이 아닙니다. `.ioc` 파일과 사용자의 실제 생성 프로젝트는 제공되지 않았으므로, 전체 생성 프로젝트나 실제 재생성 검증을 완료한 결과물은 포함하지 않습니다.

전체 배치 참고는 [`integration/main.c.example`](integration/main.c.example), [`integration/stm32f3xx_it.c.example`](integration/stm32f3xx_it.c.example)에 있습니다. 두 `.example`은 초기화 함수 전체를 생략한 **위치 설명용 파일**입니다. 원본 생성 파일에 통째로 덮어쓰거나 빌드하지 마세요.

## 1. Generate Code 전에 설정

1. [CUBEMX_SETUP.md](CUBEMX_SETUP.md)의 핀·클럭·주변장치를 `.ioc`에서 설정합니다.
2. Project Manager → Project에서 Toolchain/IDE를 **STM32CubeIDE**로 선택합니다.
3. Project Manager → Code Generator → **Keep User Code when re-generating**을 켭니다. 필요하면 백업 옵션도 켭니다.
4. `.ioc`를 저장하고 Generate Code를 실행합니다. 설정을 바꿀 때도 `.ioc`를 수정해서 재생성합니다.

`USER CODE BEGIN ...` / `USER CODE END ...`는 CubeMX가 이미 만든 이름과 위치를 그대로 사용합니다. `#include your code1` 같은 문법이 아니라, 보통 `/* USER CODE BEGIN Includes */` 안에 `#include`를 넣습니다. 구역 이름을 임의로 바꾸거나 기존 표식을 삭제하지 마세요.

## 2. 사용자 파일 복사

| 저장소 파일 | CubeIDE 프로젝트에서 넣을 위치 | 생성 주체 |
|---|---|---|
| `Core/Src/buck_app.c` | `Core/Src/buck_app.c` | 사용자가 직접 추가 |
| `Core/Inc/buck_app.h` | `Core/Inc/buck_app.h` | 사용자가 직접 추가 |
| `main.c`, `main.h` | 기존 파일 유지 | CubeMX |
| `stm32f3xx_it.c`, HAL MSP, 주변장치 초기화 | 기존 파일 유지 | CubeMX |

CubeMX의 **Generate peripheral initialization as a pair of '.c/.h' files per peripheral** 옵션이 켜져 있으면 `adc.c/.h`, `tim.c/.h` 등이 나뉘고, 꺼져 있으면 핸들과 초기화 함수가 `main.c`에 모일 수 있습니다. 이 모듈은 `extern hadc1`, `extern htim1`을 사용하므로 둘 다 지원합니다. 핸들을 추가 정의하거나 같은 `MX_*_Init()`를 복사해서 두 번 만들지 않습니다.

## 3. main.c에 직접 추가

기존 `main.c`에서 같은 구역을 찾아 **구역 안의 내용만** 추가합니다.

```c
/* USER CODE BEGIN Includes */
#include "buck_app.h" /* [직접 추가] 벅 제어 모듈 */
/* USER CODE END Includes */
```

모든 생성된 `MX_GPIO_Init`, `MX_DMA_Init`, `MX_ADC1_Init`, `MX_TIM1_Init` 호출이 끝난 다음 구역에 넣습니다. 생성된 초기화 호출 자체는 기존 파일에 한 번만 남깁니다.

```c
/* USER CODE BEGIN 2 */
/* [직접 추가] 주변장치 초기화 후 한 번만 실행 */
Buck_Init();
/* USER CODE END 2 */
```

메인 반복문에는 다음과 같이 넣습니다. **기존 닫는 중괄호도 유지**하세요. CubeMX 버전에 따라 `while`의 닫는 중괄호가 `BEGIN 3`와 `END 3` 사이에 있을 수 있으므로 구역을 통째로 치환하지 않습니다.

```c
/* USER CODE BEGIN WHILE */
while (1)
{
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* [직접 추가] ADC 프레임이 계속 갱신되는지 확인 */
  Buck_Poll();
}
/* USER CODE END 3 */
```

`HAL_TIM_PWM_Start()`나 `HAL_TIMEx_PWMN_Start()`를 추가 호출하지 않습니다. 타이머와 게이트 허용 순서는 `Buck_Init()`와 ADC 콜백이 관리합니다.

`HAL_ADC_ConvHalfCpltCallback`, `HAL_ADC_ConvCpltCallback`, `HAL_ADC_ErrorCallback`은 이미 `buck_app.c`에 있습니다. **main.c의 `USER CODE BEGIN 4`에 같은 함수를 다시 넣지 않습니다.** 기존 프로젝트에서 이 콜백을 사용했다면 정의를 한 곳으로 합칩니다. `main.h`는 따로 고칠 필요가 없습니다. `buck_app.h`의 선언을 이용하므로 함수 원형을 다시 적을 필요도 없습니다.

## 4. 오류와 HardFault에서 차단

`main.c`의 기존 `Error_Handler()`에서는 인터럽트를 먼저 끈 다음 게이트 차단을 호출하고 기존 무한 루프를 유지합니다. 오류 처리 도중 ADC 인터럽트가 기동 순서를 다시 진행하지 않도록 하는 순서입니다.

```c
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq(); /* 기존 코드 유지 */
  Buck_EmergencyOff(); /* [직접 추가] 기존 while 앞 */
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
```

`Core/Src/stm32f3xx_it.c`의 `Includes` 구역에도 헤더를 추가합니다.

```c
/* USER CODE BEGIN Includes */
#include "buck_app.h" /* [직접 추가] */
/* USER CODE END Includes */
```

기존 `HardFault_Handler()` 시작 구역에 넣습니다. 전체 함수를 새로 만들지 않습니다.

```c
/* USER CODE BEGIN HardFault_IRQn 0 */
Buck_EmergencyOff(); /* [직접 추가] 생성된 무한 루프에 들어가기 전 */
/* USER CODE END HardFault_IRQn 0 */
```

MemoryManagement, BusFault, UsageFault 핸들러를 사용한다면 해당 함수의 기존 첫 사용자 구역에도 같은 차단 호출을 추가할 수 있습니다. 원본에 없는 표식을 임의로 만들어 자동 보존을 기대하지 않습니다. 비상 차단은 독립 하드웨어 보호와 리셋 중의 외부 EN 풀다운을 대체하지 않습니다.

## 5. CubeIDE에서 빌드 확인

- 파일 복사 뒤 프로젝트를 **Refresh(F5)** 합니다.
- `buck_app.c`가 `Core/Src` 아래에 있고 활성 빌드 설정에서 **Exclude from Build** 상태가 아닌지 확인합니다. 반대로 `integration/*.example`은 설명용이므로 소스로 추가하지 않습니다.
- 컴파일러 include path에 프로젝트의 `Core/Inc`가 포함되어야 합니다. CubeMX 기본 프로젝트에는 보통 이미 있습니다.
- `buck_app.c`를 두 폴더에 복사하거나 `.c` 파일 자체를 `#include`하지 않습니다. `#include "buck_app.h"`만 사용합니다.
- `hadc1`, `htim1`, `hdma_adc1`은 CubeMX가 정의한 것을 사용합니다. 생성된 DMA/ADC IRQ의 `HAL_DMA_IRQHandler`·`HAL_ADC_IRQHandler`와 SysTick의 `HAL_IncTick`을 유지합니다.
- 최적화 `-O2`, Cortex-M4F/FPU 및 라이브러리와 맞는 hard-float ABI를 사용하고 `-ffast-math`는 쓰지 않습니다.
- Generate Code 이후 추가한 사용자 구역과 사용자 파일이 남아 있는지 diff로 확인한 뒤 Clean/Build 합니다. `.ioc`의 주변장치·핀·옵션을 바꾸면 연동 코드도 다시 확인합니다.

이 저장소는 모듈 문법 검사만 수행했으며, 사용자의 `.ioc`로 실제 Generate Code를 다시 실행하거나 ARM 링크·보드 시험을 수행하지 않았습니다.

## 공식 형식 참고

- [ST UM1718: STM32CubeMX 사용자 설명서](https://www.st.com/resource/en/user_manual/um1718-stm32cubemx-for-stm32-configuration-and-initialization-c-code-generation-stmicroelectronics.pdf): 사용자 구역과 재생성 보존 설정.
- [ST AN4989: Debug toolbox](https://www.st.com/resource/en/application_note/an4989-introduction-to-debug-toolbox-for-stm32-mcus-stmicroelectronics.pdf): HardFault 사용자 구역.
- [ST 공식 CubeMX 형식 예제](https://github.com/STMicroelectronics/STM32CubeG4/blob/master/Projects/STM32G474E-EVAL/Examples/OPAMP/OPAMP_PGA/Src/main.c): `Includes`, `2`, `3`, `4`, `Error_Handler_Debug` 배치. 제품별 초기화 값이 아니라 구역 형식만 참고합니다.
- [ST F303ZE 템플릿](https://github.com/STMicroelectronics/STM32CubeF3/blob/master/Projects/STM32F303ZE-Nucleo/Templates/Src/main.c): 해당 MCU의 HSI/PREDIV2×16 64MHz 설정.
