/* [직접 작성하는 파일] Core/Inc/buck_app.h
 * 생성된 main.h를 덮어쓰지 말고 이 헤더를 별도 파일로 추가합니다.
 * main.c/stm32f3xx_it.c의 기존 USER CODE Includes 안에서 include합니다. */
#ifndef BUCK_APP_H
#define BUCK_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* main.c: USER CODE 2. GPIO/DMA/ADC1/TIM1 초기화 뒤 한 번 호출합니다. */
void Buck_Init(void);
/* main.c: USER CODE 3. 메인 반복문 안에서 계속 호출합니다. */
void Buck_Poll(void);
/* 전용 EN LOW + TIM1 MOE 해제. 오류 처리 구역의 정지 루프 전에 호출합니다.
 * Error_Handler/HardFault는 호출 후 기존 정지 루프에 머물러야 합니다.
 * 정상 실행 중 정지할 때는 buck_arm_request=2를 사용합니다. */
void Buck_EmergencyOff(void);

/* 디버거 명령: 1=시작, 2=정지, 3=고장 해제(자동 재시작하지 않음).
 * 시작 조건: BUCK_BOARD_VERIFIED=1, 영점 보정 완료, 유효한 센서 값. */
extern volatile uint32_t buck_arm_request;

#ifdef __cplusplus
}
#endif
#endif
