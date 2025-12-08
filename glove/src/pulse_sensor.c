#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include "pulse_sensor.h"

#define PULSE_SAMPLE_PERIOD_MS  2 

static volatile uint16_t g_pulse_raw = 0;     
static volatile uint16_t g_bpm = 0;           
static volatile uint8_t  g_beat_flag = 0;     

static volatile uint32_t sampleCounter = 0;  
static volatile uint32_t lastBeatTime = 0;    

static volatile uint16_t thresh = 512;      
static volatile uint16_t peak  = 512;         
static volatile uint16_t trough= 512;         
static volatile uint16_t amp   = 100;        

static volatile uint8_t  Pulse = 0;           
static volatile uint16_t IBI   = 600;         
static volatile uint8_t  firstBeat = 1;
static volatile uint8_t  secondBeat = 0;

static volatile uint16_t rate[10] = {0};

static void adc_init(uint8_t channel);
static void timer1_init(void);
static void process_sample(uint16_t signal);

void PulseSensor_Init(uint8_t adc_channel)
{
    cli(); 

    adc_init(adc_channel);
    timer1_init();

    sampleCounter = 0;
    lastBeatTime  = 0;
    thresh        = 512;
    peak          = 512;
    trough        = 512;
    amp           = 100;
    Pulse         = 0;
    IBI           = 600;
    firstBeat     = 1;
    secondBeat    = 0;
    for (uint8_t i = 0; i < 10; i++) {
        rate[i] = IBI;
    }
    g_bpm        = 0;
    g_beat_flag  = 0;

    sei();  
}

uint16_t PulseSensor_GetBPM(void)
{
    return g_bpm;
}

uint8_t PulseSensor_IsBeat(void)
{
    uint8_t flag = g_beat_flag;
    g_beat_flag = 0;
    return flag;
}

uint16_t PulseSensor_GetRawSignal(void)
{
    return g_pulse_raw;
}

static void adc_init(uint8_t channel)
{
    ADMUX = (1 << REFS0);              
    ADMUX |= (channel & 0x0F);        

    ADCSRA = (1 << ADEN)  | 
             (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); 
}

static void timer1_init(void)
{
    TCCR1A = 0;
    TCCR1B = 0;

    OCR1A = 500 - 1; 

    TCCR1B |= (1 << WGM12) | (1 << CS11) | (1 << CS10); 
    TIMSK1 |= (1 << OCIE1A);
}

ISR(TIMER1_COMPA_vect)
{
    sampleCounter += PULSE_SAMPLE_PERIOD_MS;  


    ADCSRA |= (1 << ADSC);


    while (ADCSRA & (1 << ADSC));

    uint16_t value = ADC;          
    g_pulse_raw = value;          
    process_sample(value);         
}

static void process_sample(uint16_t signal)
{
    uint16_t N = sampleCounter;  


    if (signal < thresh && signal < trough) {
        trough = signal;
    }
    if (signal > thresh && signal > peak) {
        peak = signal;
    }


    if ((N - lastBeatTime) > 250) {
        if ((signal > thresh) && (Pulse == 0)) {

            Pulse = 1;
            IBI = N - lastBeatTime;
            lastBeatTime = N;


            if (IBI < 320 || IBI > 2200) {
                return; 
            }
            // --------------------------------------------------

            if (firstBeat) {
                firstBeat = 0;
                secondBeat = 1;
                return;
            }

            if (secondBeat) {
                secondBeat = 0;
                for (uint8_t i = 0; i < 10; i++) {
                    rate[i] = IBI;
                }
            }


            uint16_t curAmp = (peak > trough) ? (peak - trough) : 0;
            if (curAmp < 20) {

                return;
            }

            uint32_t runningTotal = 0;
            for (uint8_t i = 0; i < 9; i++) {
                rate[i] = rate[i+1];
                runningTotal += rate[i];
            }
            rate[9] = IBI;
            runningTotal += rate[9];

            uint32_t avg_IBI = runningTotal / 10;
            if (avg_IBI > 0) {
                uint16_t bpm = (uint16_t)(60000UL / avg_IBI);


                if (bpm >= 40 && bpm <= 170) {
                    g_bpm = bpm;
                    g_beat_flag = 1;  
                }

            } else {
                g_bpm = 0;
            }
        }
    }


    if ((signal < thresh) && (Pulse == 1)) {
        Pulse = 0;
        amp = peak - trough;          

        thresh = trough + (amp / 2);

        peak = thresh;
        trough = thresh;
    }


    if ((N - lastBeatTime) > 2500) {  
        thresh     = 512;
        peak       = 512;
        trough     = 512;
        lastBeatTime = N;
        firstBeat  = 1;
        secondBeat = 0;
        Pulse      = 0;
        g_bpm      = 0;
        amp        = 100;
    }
}
