#include "../Setting/configuration.h"
#include "../Application/motor.h"
#include "ports.h"
#include "pwm.h"
#include "../Application/step.h"
//デフォルト値
#define PWM_PERIOD  ((0x031F))//0C7C)//063E)//031F)//現在約50kHz 0000 0110 0011 1110 増やすと減る
#define PWM_DT_MAX      (0.95)
#define PWM_DT_MIN      (0.15)
#define CNT_PERIOD      (50)//ms
static uint16_t pwm_period = PWM_PERIOD;
//Q16 Formatで生で保存
static Q16_t pwm_max = UINT16_MAX*PWM_DT_MAX;
static Q16_t pwm_max_raw= (uint32_t)(UINT16_MAX*PWM_DT_MAX*PWM_PERIOD)>>16;
static Q16_t pwm_min = UINT16_MAX*PWM_DT_MIN;
static Q16_t pwm_min_raw=(uint32_t)(UINT16_MAX*PWM_DT_MIN*PWM_PERIOD)>>16;

//内部関数群
inline uint16_t dt_limit(uint16_t); //dt比をdt_max～dt_min,0の間の値に設定する

const int16_t poss=0x7fff;
static int16_t rate=0;
static uint32_t pid_period=(uint32_t)((float)(0x031F)/PWM_PERIOD*24*CNT_PERIOD);//現在50ms
static uint16_t  event=0;
static event_t events=NULL;
static int32_t step_rate=0;
static bool step_flag=false;
static uint32_t hz=0; 
void pwm_setup() {

    //initialize module
    ST_PIN = false; //force stop

    PTCONBITS ptcon; //about clocks
    ptcon.PTEN = false; //module off
    ptcon.PTSIDL = false; //idle on
    ptcon.PTOPS = 1; //post priscaler (=2^)
    ptcon.PTCKPS = 0; //clock priscaler
    ptcon.PTMOD = 0b00; //free run mode
    PTCONbits = ptcon;

    PTMR = 0; //counter
    PTPER = pwm_period >> 1; //period

    PWMCON1BITS pcon1;
    pcon1.PMOD2 = true; //独立モード
    pcon1.PMOD3 = true;
    pcon1.PEN1H = false; //GPIO
    pcon1.PEN1L = false; //GPIO
    pcon1.PEN2H = true; //PWM
    pcon1.PEN2L = false; //GPIO(LED)
    pcon1.PEN3H = true; //PWM
    pcon1.PEN3L = false; //GPIO(ST)
    PWMCON1bits = pcon1;

    PWMCON2 = 0; //invale
    DTCON1 = 0; //invale
    FLTACON = 0; //invale

    OVDCONBITS ov;
    ov.POVD2H = true; //form pwm generator
    ov.POVD3H = true;
    OVDCONbits = ov;

    PDC2 = 0;
    PDC3 = 0; //stopping

    IFS2bits.PWMIF=false;
     IPC9bits.PWMIP=6;
     IEC2bits.PWMIE=true;
    //wake up
    PTCONbits.PTEN = true;
    ST_PIN = false;
}

inline uint16_t dt_limit(uint16_t var) {
    if (var > pwm_max_raw) {
        return pwm_max_raw;
    } else if (var < pwm_min_raw) {
        return 0;
    } else {
        return var;
    }
}

inline void pwm_dt_direct(uint16_t dt, triple_t dir) {
    //データ確認
    dt = dt_limit(dt);
    dir = (dt == 0) ? zero : dir;

    if (dir == plus) {
        PDC2 = dt;
        PDC3 = 0;
    } else if (dir == minus) {
        PDC2 = 0;
        PDC3 = dt;
    } else {
        PDC2 = 0;
        PDC3 = 0;
    }
}

inline void pwm_dt(Q16_t dt, triple_t dir) {
    Q16_t temp = ((uint32_t) dt * pwm_period) >> 15;
    pwm_dt_direct(temp, dir);
}

inline void pwm_dts(Q15_t dt) {
    Q16_t dt_abs = dt > 0 ? dt : -dt;
    triple_t dir = dt > 0 ? plus : minus;
    pwm_dt(dt_abs, dir);
}

inline void set_pwm_period(uint16_t period) {
    PTPER = period >> 1; //period
    pwm_period=period;
    pwm_max_raw=((uint32_t)pwm_max*period)>>16;
    pwm_min_raw=((uint32_t)pwm_min*period)>>16;
}

inline uint16_t get_pwm_period(){
    return pwm_period;
}

inline void set_pwm_max(uint16_t max){
    pwm_max=max;
    pwm_max_raw=((uint32_t)pwm_period*max)>>16;
}

inline uint16_t get_pwm_max(){
    return pwm_max;
}

inline void set_pwm_min(uint16_t min){
    pwm_min=min;
    pwm_min_raw=((uint32_t)pwm_period*min)>>16;
}

inline uint16_t get_pwm_min(){
    return pwm_min;
}

inline Q16_t  get_pwm_dt(){
    //現在のモーター状態を確認
    uint16_t raw;
    raw= PDC2>PDC3?PDC2:PDC3;//最大値をもとめる
    
    return ((uint32_t)raw<<16)/pwm_period;
}

inline void pwm_shutdown(bool fag) {
    ST_PIN = !fag;
}


void pid_enable(uint16_t x){
    event=x;
}
void pwm_pid_period(uint16_t period){
    pid_period=period;
}

inline int16_t pid_rate(){
    return rate;
}

void set_step_rate(){
    step_flag=true;
}

int32_t get_step_rate(){
    return (step_rate+POSCNT-poss);
}


float get_period_s(){
    return (float)(PWM_PERIOD*pid_period/19176)/1000;
}

float get_period_ms(){
    return (float)(PWM_PERIOD*pid_period/19176);
}

void _ISR _PWMInterrupt(){
static uint16_t cnt;
    if(cnt==pid_period/*(++cnt & 0xFF)==0x01*/){
        //LATEbits.LATE2=~LATEbits.LATE2;
     rate=POSCNT-poss;
     if(step_flag==true)step_rate+=rate;
     if(events!=NULL){
     events();
     }
     POSCNT=poss;
     cnt=0;
    }
    cnt++;
    IFS2bits.PWMIF=false;
}

void set_event(event_t func){
    /*if(func == NULL){
        
    }else{
        func();
    }*/
    events=func;
}

void _ISR _CNInterrupt(){
       hz=0x0000FFFF&TMR4;
       uint32_t c=TMR5HLD;
       hz=hz+(c<<16);
       hz=hz/20;
       //float cn_period=1000/(float)hz;
       pid_period=1000000/hz;//20000*cn_period;
    TMR4=0;
    TMR5=0;
    TMR5HLD=0;
    IFS0bits.CNIF=false;
}

void get_mc_period(uint32_t *Hz,uint32_t *P){
     *Hz=hz;
     *P=pid_period;
}
