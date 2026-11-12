/* [직접 작성하는 파일] Core/Src/buck_app.c
 * CubeMX 자동 생성 파일이 아니므로 이 파일 전체를 프로젝트에 추가합니다.
 * main.c에는 CUBEMX_USER_CODE.md에 나온 기존 USER CODE 구역 안에만
 * #include "buck_app.h", Buck_Init(), Buck_Poll()을 추가합니다.
 * HAL ADC 콜백은 이 파일에 있으므로 main.c에 중복 작성하지 않습니다.
 * 클럭/핀/ADC/PWM의 MX_*_Init() 설정은 CubeMX에서 생성합니다. */
#include "stm32f3xx_hal.h"
#include "buck_app.h"
#include <math.h>
#include <string.h>

/* [직접 확인/수정] 배선, ADC 환산, 게이트 파형을 확인한 뒤 1로 변경합니다.
 * 기본 0에서는 영점 보정과 디버거 관찰만 가능하며 전력단은 기동하지 않습니다.
 * PC6는 외부 풀다운이 있는 전용 DRV_EN이어야 합니다. ACS71240의
 * open-drain FAULT와 공유된 선을 push-pull HIGH로 강제 구동하지 마세요. */
#define BUCK_BOARD_VERIFIED 0u
#define VDDA 3.300f
#define CURRENT_POLARITY 1.0f /* IP+ -> IP-의 실제 전류 방향을 확인합니다. */
#define PERIOD 1280u          /* TIM1 = HCLK = 64MHz일 때 명목 50kHz */
#define TRIGGER 320u          /* 주기 시작 5us 후 OC4REF 상승 -> ADC 시작 */
#define MARGIN 64u            /* 다음 PWM update의 1us 전까지 계산 완료 */
/* CubeMX가 정의한 핸들을 참조합니다. 여기에서 다시 정의하지 않습니다.
 * 초기화 함수가 main.c에 있거나 adc.c/tim.c로 나뉘어 있어도 동일합니다. */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;
enum { DISARMED, PRECHARGE, SOFTSTART, RUN, FAULT };
enum { F_ADC=1, F_ZERO=2, F_TIMING=4, F_VIN=8, F_OVP=16,
       F_OC=32, F_REVERSE=64, F_PREBIAS=128, F_STARTUP=256, F_SETUP=512 };
/* 디버거 명령: 1=시작, 2=정지, 3=고장 해제(재시작하지 않음) */
volatile uint32_t buck_arm_request;
volatile struct {
    float vin, vout, il, zero, duty;
    uint32_t state, faults, calibrated, frames, max_cycles, verified;
} buck_debug;
/* CubeMX: DMA Circular/Halfword, ADC Rank 순서 IL,Vout,Vin.
 * 3개씩 두 프레임을 받아 Half/Full 완료 콜백으로 처리합니다.
 * DMA가 접근하지 못하는 CCM RAM에 이 버퍼를 배치하면 안 됩니다. */
static volatile uint16_t adc_frame[6];
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
    /* Error_Handler는 주변장치 초기화 전에도 호출될 수 있습니다.
     * 아직 클럭이 꺼져 있는 주변장치에는 접근하지 않습니다.
     * 리셋/초기화 전 OFF 상태는 PC6의 외부 EN 풀다운이 보장해야 합니다. */
    if (__HAL_RCC_GPIOC_IS_CLK_ENABLED()) {
        GPIOC->BSRR = (uint32_t)GPIO_PIN_6 << 16u;
    }
    if (__HAL_RCC_TIM1_IS_CLK_ENABLED()) {
        TIM1->BDTR &= ~TIM_BDTR_MOE;
    }
    /* CCR1=0만으로 정지하면 상보 저측 MOSFET은 계속 켜질 수 있습니다. */
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

/* ADC DMA 인터럽트에서 20us마다 실행됩니다. printf/HAL_Delay 등
 * 블로킹 코드를 넣지 말고 필요한 값은 buck_debug로 관찰합니다. */
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
        if (b.state!=FAULT) b.state=DISARMED; /* 정지 명령은 고장을 해제하지 않음 */
    } else if (request==3u && (b.state==FAULT || b.state==DISARMED)) {
        Buck_EmergencyOff(); reset_control(); b.faults=0u; b.state=DISARMED;
    }
    /* 양쪽 게이트 OFF 상태에서 2048표본(약 41ms)으로 영점을 보정합니다.
     * 외부에서 흐르는 인덕터 전류도 없어야 합니다.
     * 전압 측정값이 0이라는 사실만으로 센서 배선이 정상임을 알 수는 없습니다. */
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
            b.state=PRECHARGE; b.boot=52u; /* 2주기 대기 + 저측만 50주기 충전 */
        }
    } /* 실패한 시작 요청은 폐기합니다. 이후 조건이 맞아도 자동 기동하지 않습니다. */
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
                GPIOC->BSRR=GPIO_PIN_6; /* CCR1=0: 저측만 켜서 bootstrap 충전 */
            }
            if (--b.boot==0u) b.state=SOFTSTART;
        }
    } else if (b.state==SOFTSTART || b.state==RUN) {
        float requested, applied;
        if (!(TIM1->BDTR & TIM_BDTR_MOE)) trip(F_SETUP);
        if (b.state!=FAULT) {
            if (b.count<50000u) ++b.count; /* 기동 시간 계수는 1초에서 포화 */
            /* 기준전압 0 -> 15V의 300ms 램프. 실제 출력 정착 시간과 다릅니다. */
            b.ref=b.count<15000u ? 15.0f*((float)b.count/15000.0f) : 15.0f;
            if (b.count>=15000u) b.state=RUN;
            if (!b.checked && b.count>=50000u) {
                if (vout<13.5f) trip(F_STARTUP);
                else b.checked=1u;
            }
            /* 전류 표본에 따른 듀티 제한입니다. 정전류(CC) 루프나
             * 주기별 피크 보호가 아닙니다. 임계값은 실제 리플에 맞춰 조정합니다. */
            if (il>1.15f) b.cap-=20.0f/50000.0f;
            else if (il<1.05f) b.cap+=0.5f/50000.0f;
            b.cap=clamp(b.cap,0.0f,0.75f);
            if (b.phase==0u) {
                float error=b.ref-vout;
                b.integ=clamp(b.integ+b.aw+0.25f*error*0.0001f,-0.25f,0.25f);
                b.aw=0.0f; b.prop=0.005f*error; /* 이득은 튜닝용 초깃값 */
            }
            if (++b.phase==5u) b.phase=0u; /* 5표본마다 전압 피드백: 10kHz */
            requested=b.ref/vin+b.prop+b.integ; /* 입력전압 피드포워드: 50kHz */
            applied=clamp(requested,0.0f,b.cap);
            applied=clamp(applied,0.0f,b.duty+1.0f/50000.0f); /* 상승률 1.0/s */
            /* 전류 제한/상승률 제한 후 최종 명령 듀티를 추적하는 anti-windup */
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

/* [직접 작성하는 HAL 콜백] HAL의 weak 기본 함수를 재정의합니다.
 * main.c의 USER CODE 4 또는 다른 .c에 같은 함수를 중복 정의하지 마세요.
 * stm32f3xx_it.c의 자동 생성 HAL_DMA_IRQHandler/HAL_ADC_IRQHandler는
 * 그대로 둡니다. 해당 HAL 처리 후 아래 콜백이 호출됩니다. */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc)
{ if (adc->Instance==ADC1) frame(0u); }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{ if (adc->Instance==ADC1) frame(1u); }
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{ if (adc->Instance==ADC1) trip(F_ADC); }

void Buck_Init(void)
{
    /* main.c의 USER CODE 2에서 모든 MX_*_Init() 뒤 한 번만 호출합니다. */
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
    /* main.c에서 HAL_TIM_PWM_Start/HAL_TIMEx_PWMN_Start를 별도로 호출하면
     * MOE가 조기에 켜질 수 있습니다. 타이머 시작 순서는 이 함수가 관리합니다. */
    TIM1->CCER |= TIM_CCER_CC1E | TIM_CCER_CC1NE;
    TIM1->EGR=TIM_EGR_UG; TIM1->SR=0u; TIM1->CNT=0u;
    Buck_EmergencyOff(); TIM1->CR1 |= TIM_CR1_CEN;
}

void Buck_Poll(void)
{
    /* main.c의 USER CODE 3, while(1) 안에서 계속 호출합니다. */
    static uint32_t seen, fresh_ms;
    uint32_t now=HAL_GetTick(), frames=buck_debug.frames;
    if (frames!=seen) { seen=frames; fresh_ms=now; }
    else if ((uint32_t)(now-fresh_ms)>1u) {
        uint32_t mask=__get_PRIMASK();
        __disable_irq(); trip(F_TIMING); __set_PRIMASK(mask);
    }
}
