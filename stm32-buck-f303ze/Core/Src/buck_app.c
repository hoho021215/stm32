/* Add to a CubeMX STM32F303ZE project; do not duplicate these ADC callbacks.
 * main: after MX_GPIO/DMA/ADC1/TIM1_Init(): Buck_Init();
 *       in while(1): Buck_Poll();
 * Add prototypes in main.c: void Buck_Init(void); void Buck_Poll(void);
 * HardFault/Error handler: call Buck_EmergencyOff() before stopping.
 * This module needs the exact CubeMX settings described with the example. */
#include "stm32f3xx_hal.h"
#include "buck_app.h"
#include <math.h>
#include <string.h>

#define BUCK_BOARD_VERIFIED 0u /* Set 1 only after board/sensing/gate verification. */
/* PC6 must be a DEDICATED DRV_EN net with external pulldown. Never drive a
 * shared ACS71240 open-drain FAULT/EN net high; separate it or design enable
 * logic first. BUCK_BOARD_VERIFIED stays zero until this wiring is checked. */
#define VDDA 3.300f
#define CURRENT_POLARITY 1.0f /* Verify IP+ -> IP- direction. */
#define PERIOD 1280u          /* TIM1 = HCLK = 64 MHz; PWM = 50 kHz. */
#define TRIGGER 320u          /* OC4REF rises at 5 us. */
#define MARGIN 64u            /* Complete at least 1 us before update. */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;
enum { DISARMED, PRECHARGE, SOFTSTART, RUN, FAULT };
enum { F_ADC=1, F_ZERO=2, F_TIMING=4, F_VIN=8, F_OVP=16,
       F_OC=32, F_REVERSE=64, F_PREBIAS=128, F_STARTUP=256, F_SETUP=512 };
volatile uint32_t buck_arm_request; /* Debugger: 1=start, 2=stop, 3=clear. */
volatile struct {
    float vin, vout, il, zero, duty;
    uint32_t state, faults, calibrated, frames, max_cycles, verified;
} buck_debug;
static volatile uint16_t adc_frame[6]; /* [IL,Vout,Vin] twice; HT then TC. */
static struct {
    uint32_t state, faults, count, phase, boot, checked;
    uint32_t zero_sum, zero_n, calibrated, previous, previous_valid, expect;
    uint16_t zero_min, zero_max;
    float zero, ref, integ, prop, aw, cap, duty;
} b;

static float clamp(float x, float lo, float hi)
{ return x < lo ? lo : (x > hi ? hi : x); }

void Buck_EmergencyOff(void)
{
    GPIOC->BSRR = (uint32_t)GPIO_PIN_6 << 16u; /* UCC27714 EN low. */
    TIM1->BDTR &= ~TIM_BDTR_MOE; /* CCR=0 alone leaves low-side ON. */
}

static void trip(uint32_t fault)
{
    Buck_EmergencyOff();
    b.faults |= fault; b.state = FAULT; b.boot = 0u; b.duty = 0.0f;
    buck_debug.state = FAULT; buck_debug.faults = b.faults;
    buck_debug.duty = 0.0f;
}

static void reset_control(void)
{
    b.count=0u; b.phase=0u; b.boot=0u; b.checked=0u;
    b.ref=0.0f; b.integ=0.0f; b.prop=0.0f; b.aw=0.0f;
    b.cap=0.75f; b.duty=0.0f;
}

static void frame(uint32_t half)
{
    uint32_t entered=DWT->CYCCNT, counter=TIM1->CNT, offset=half*3u;
    uint32_t request=buck_arm_request, elapsed;
    uint16_t ri=adc_frame[offset], rv=adc_frame[offset+1u];
    uint16_t rin=adc_frame[offset+2u];
    float vi=(float)ri*(VDDA/4095.0f);
    float vin=(float)rin*(VDDA*19.0f/4095.0f);
    float vout=(float)rv*(VDDA*19.0f/4095.0f);
    float il=CURRENT_POLARITY*(vi-b.zero)/0.132f;
    buck_arm_request=0u; ++buck_debug.frames;
    if (b.previous_valid) {
        uint32_t interval=entered-b.previous;
        if (interval < PERIOD/2u || interval > PERIOD+PERIOD/2u) trip(F_TIMING);
    }
    b.previous=entered; b.previous_valid=1u;
    if (counter < TRIGGER || counter >= PERIOD-MARGIN) trip(F_TIMING);
    if (half != b.expect) trip(F_ADC);
    b.expect=half ? 0u : 1u;
    if (ri<16u || ri>4079u || rv>4079u || rin>4079u) trip(F_ADC);
    if (request==2u) {
        Buck_EmergencyOff(); reset_control();
        if (b.state!=FAULT) b.state=DISARMED; /* STOP cannot clear a fault. */
    } else if (request==3u && (b.state==FAULT || b.state==DISARMED)) {
        Buck_EmergencyOff(); reset_control(); b.faults=0u; b.state=DISARMED;
    }
    /* Calibration requires both gates OFF and zero external inductor current.
     * Do not assume a near-zero feedback reading proves an intact sensor. */
    if (!b.calibrated && b.state==DISARMED) {
        Buck_EmergencyOff(); b.zero_sum+=ri;
        if (ri<b.zero_min) b.zero_min=ri;
        if (ri>b.zero_max) b.zero_max=ri;
        if (++b.zero_n==2048u) {
            b.zero=(float)b.zero_sum*(VDDA/(4095.0f*2048.0f));
            if (b.zero<1.45f || b.zero>1.85f ||
                (float)(b.zero_max-b.zero_min)*(VDDA/4095.0f)>0.15f) {
                b.zero_n=0u; b.zero_sum=0u; b.zero_min=4095u; b.zero_max=0u;
                trip(F_ZERO);
            } else b.calibrated=1u;
            il=CURRENT_POLARITY*(vi-b.zero)/0.132f;
        }
    }
    if (request==1u && b.state==DISARMED && b.calibrated && BUCK_BOARD_VERIFIED) {
        if (vin<23.0f || vin>36.0f) trip(F_VIN);
        else if (vout>0.5f) trip(F_PREBIAS);
        else {
            reset_control(); TIM1->CCR1=0u;
            b.state=PRECHARGE; b.boot=52u; /* 2 settling + 50 low-only frames. */
        }
    } /* Failed start requests are discarded; no delayed automatic start. */
    if (b.state==PRECHARGE || b.state==SOFTSTART || b.state==RUN) {
        if (!isfinite(vin) || !isfinite(vout) || !isfinite(il)) trip(F_ADC);
        else if (vin<23.0f || vin>36.0f) trip(F_VIN);
        else if (vout>=17.0f) trip(F_OVP);
        else if (il>=1.8f) trip(F_OC);
        else if (il<=-0.4f) trip(F_REVERSE);
    }
    if (b.state==PRECHARGE) {
        if (vout>0.5f) trip(F_PREBIAS);
        else {
            if (b.boot<=50u) {
                TIM1->BDTR |= TIM_BDTR_MOE;
                GPIOC->BSRR=GPIO_PIN_6; /* CCR1=0: bootstrap LOW side only. */
            }
            if (--b.boot==0u) b.state=SOFTSTART;
        }
    } else if (b.state==SOFTSTART || b.state==RUN) {
        float requested, applied;
        if (!(TIM1->BDTR & TIM_BDTR_MOE)) trip(F_SETUP);
        if (b.state!=FAULT) {
            if (b.count<50000u) ++b.count; /* Saturating startup counter. */
            b.ref=b.count<15000u ? 15.0f*((float)b.count/15000.0f) : 15.0f;
            if (b.count>=15000u) b.state=RUN;
            if (!b.checked && b.count>=50000u) {
                if (vout<13.5f) trip(F_STARTUP);
                else b.checked=1u;
            }
            /* Supervisory duty cap, not CC control or peak-current protection. */
            if (il>1.15f) b.cap-=20.0f/50000.0f;
            else if (il<1.05f) b.cap+=0.5f/50000.0f;
            b.cap=clamp(b.cap,0.0f,0.75f);
            if (b.phase==0u) {
                float error=b.ref-vout;
                b.integ=clamp(b.integ+b.aw+0.25f*error*0.0001f,-0.25f,0.25f);
                b.aw=0.0f; b.prop=0.005f*error; /* Placeholder gains. */
            }
            if (++b.phase==5u) b.phase=0u; /* Independent 10 kHz feedback. */
            requested=b.ref/vin+b.prop+b.integ; /* Vin feedforward, 50 kHz. */
            applied=clamp(requested,0.0f,b.cap);
            applied=clamp(applied,0.0f,b.duty+1.0f/50000.0f);
            b.aw+=50.0f*(applied-requested)/50000.0f;
            if (b.state!=FAULT) {
                b.duty=applied;
                TIM1->CCR1=(uint32_t)(applied*(float)PERIOD+0.5f);
            }
        }
    }
    if (b.state==FAULT || b.state==DISARMED) {
        Buck_EmergencyOff(); TIM1->CCR1=0u; b.duty=0.0f;
    }
    buck_debug.vin=vin; buck_debug.vout=vout; buck_debug.il=il;
    buck_debug.zero=b.zero; buck_debug.duty=b.duty;
    buck_debug.state=b.state; buck_debug.faults=b.faults;
    buck_debug.calibrated=b.calibrated; buck_debug.verified=BUCK_BOARD_VERIFIED;
    elapsed=DWT->CYCCNT-entered;
    if (elapsed>buck_debug.max_cycles) buck_debug.max_cycles=elapsed;
    if (counter<TRIGGER || counter>=PERIOD-MARGIN ||
        elapsed>=PERIOD-counter-MARGIN) trip(F_TIMING);
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc)
{ if (adc->Instance==ADC1) frame(0u); }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{ if (adc->Instance==ADC1) frame(1u); }
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{ if (adc->Instance==ADC1) trip(F_ADC); }

void Buck_Init(void)
{
    Buck_EmergencyOff(); memset(&b,0,sizeof b); reset_control();
    b.zero=1.65f; b.zero_min=4095u;
    memset((void *)&buck_debug,0,sizeof buck_debug);
    buck_arm_request=0u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT=0u; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (HAL_RCC_GetHCLKFreq()!=64000000u || htim1.Instance!=TIM1 ||
        hadc1.Instance!=ADC1 || TIM1->PSC!=0u || TIM1->ARR!=PERIOD-1u ||
        TIM1->CCR4!=TRIGGER || !(TIM1->CCMR1 & TIM_CCMR1_OC1PE) ||
        (TIM1->CCMR1 & TIM_CCMR1_OC1M)!=TIM_OCMODE_PWM1 ||
        (TIM1->CCMR1 & TIM_CCMR1_CC1S)!=0u ||
        (TIM1->CCMR2 & TIM_CCMR2_OC4M)!=(TIM_OCMODE_PWM2 << 8u) ||
        (TIM1->CCER & (TIM_CCER_CC1P | TIM_CCER_CC1NP))!=0u ||
        (TIM1->CR2 & (TIM_CR2_OIS1 | TIM_CR2_OIS1N))!=0u ||
        (TIM1->CR2 & TIM_CR2_MMS)!=TIM_TRGO_OC4REF ||
        (TIM1->CR1 & TIM_CR1_CKD)!=0u ||
        (TIM1->BDTR & TIM_BDTR_DTG)!=19u ||
        !(TIM1->BDTR & TIM_BDTR_OSSI) || (TIM1->BDTR & TIM_BDTR_AOE)) {
        trip(F_SETUP); return;
    }
    TIM1->CR1 &= ~TIM_CR1_CEN; TIM1->CCR1=0u;
    if (HAL_ADCEx_Calibration_Start(&hadc1,ADC_SINGLE_ENDED)!=HAL_OK ||
        HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)adc_frame,6u)!=HAL_OK) {
        trip(F_ADC); return;
    }
    /* Do not call HAL_TIM_PWM_Start/StartN: those can set MOE prematurely. */
    TIM1->CCER |= TIM_CCER_CC1E | TIM_CCER_CC1NE;
    TIM1->EGR=TIM_EGR_UG; TIM1->SR=0u; TIM1->CNT=0u;
    Buck_EmergencyOff(); TIM1->CR1 |= TIM_CR1_CEN;
}

void Buck_Poll(void)
{
    static uint32_t seen, fresh_ms;
    uint32_t now=HAL_GetTick(), frames=buck_debug.frames;
    if (frames!=seen) { seen=frames; fresh_ms=now; }
    else if ((uint32_t)(now-fresh_ms)>1u) {
        uint32_t mask=__get_PRIMASK();
        __disable_irq(); trip(F_TIMING); __set_PRIMASK(mask);
    }
}
