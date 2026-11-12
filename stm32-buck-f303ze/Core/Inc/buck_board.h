/* [직접 작성/수정하는 파일]
 * ex1.ioc의 MCU와 72MHz 클럭을 기준으로 한 연동 설정입니다.
 * 아래 ADC/TIM 목표값은 아직 ex1.ioc에 반영되지 않았습니다.
 * 사용자가 추후 올릴 .ioc와 배선을 확인한 뒤 함께 확정합니다. */
#ifndef BUCK_BOARD_H
#define BUCK_BOARD_H

#include "stm32f3xx_hal.h"

/* 배선/센싱/데드타임/보호 검증 전에는 0을 유지합니다.
 * 0이어도 ADC/TIM이 불완전하면 초기화는 F_SETUP으로 종료됩니다. */
#define BUCK_BOARD_VERIFIED       0u

/* ex1.ioc: HSE 8MHz(ST-LINK MCO 경로), PLL x9, HCLK/TIM1 72MHz.
 * 기존 USB 48MHz(72/1.5)를 유지하기 위해 64MHz로 바꾸지 않습니다. */
#define BUCK_CORE_CLOCK_HZ        72000000u
#define BUCK_TIMER_CLOCK_HZ       72000000u

/* [추후 TIM 설정용 임시 목표] 현재 원본은 Center-aligned/ARR3599입니다.
 * 이 제어 모듈의 목표는 Up/PSC0/ARR1439, 50kHz입니다. 원본을 자동 변경하지 않습니다. */
#define BUCK_PWM_HZ               50000u
#define BUCK_PERIOD_TICKS         (BUCK_TIMER_CLOCK_HZ / BUCK_PWM_HZ)
#define BUCK_TRIGGER_TICKS        (BUCK_TIMER_CLOCK_HZ / 200000u)  /* 5us */
#define BUCK_MARGIN_TICKS         (BUCK_TIMER_CLOCK_HZ / 1000000u) /* 1us */
#define BUCK_DEADTIME_TICKS       22u /* CKD DIV1일 때 약 305.6ns. 튜닝 전 초깃값 */

/* ex1.ioc의 PWM 핀: PC0 / AF2 = TIM1_CH1, PA7 / AF6 = TIM1_CH1N.
 * PWM 핀/AF는 CubeMX에서 생성하며 이 헤더가 GPIO를 설정하지는 않습니다.
 * 추후 ADC 목표: PA0=IL, PA1=Vout, PA2=Vin. 현재 원본은 PA0 하나뿐입니다. */
#define BUCK_ADC_VDDA_V           3.300f /* PCC.Vdd와 다름. 실제 VDDA를 측정해 보정 */
#define BUCK_CURRENT_POLARITY     1.0f   /* 정상 IP+ -> IP- 전류에서 양수인지 확인 */

/* [배선 확인 대기] 원본 ex1.ioc에는 PC6 EN 출력이 없습니다.
 * 전용 EN+외부 풀다운을 구성한 뒤 CubeMX에 GPIO Output LOW로 추가합니다.
 * 센서 open-drain FAULT와 공유된 선을 push-pull HIGH로 구동하지 마세요. */
#define BUCK_EN_GPIO              GPIOC
#define BUCK_EN_PIN               GPIO_PIN_6
#define BUCK_EN_CLOCK_ENABLED()   __HAL_RCC_GPIOC_IS_CLK_ENABLED()

#if BUCK_CORE_CLOCK_HZ != BUCK_TIMER_CLOCK_HZ
#error "DWT timing checks require HCLK equal to TIM1 clock"
#endif
#if BUCK_PWM_HZ != 50000u
#error "Controller coefficients currently assume 50 kHz sampling"
#endif

#endif
