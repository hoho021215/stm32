#ifndef BUCK_APP_H
#define BUCK_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after the generated GPIO, DMA, ADC1 and TIM1 initialisation. */
void Buck_Init(void);
/* Call repeatedly from the foreground loop. */
void Buck_Poll(void);
/* Immediately drive the dedicated EN low and clear TIM1 MOE.
 * Error/HardFault handlers must remain stopped after calling this function. */
void Buck_EmergencyOff(void);

/* 1 = explicit start, 2 = stop, 3 = clear fault without starting.
 * Start requires BUCK_BOARD_VERIFIED=1 and completed zero calibration. */
extern volatile uint32_t buck_arm_request;

#ifdef __cplusplus
}
#endif
#endif
