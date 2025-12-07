#ifndef PULSE_SENSOR_H
#define PULSE_SENSOR_H

#include <stdint.h>


void PulseSensor_Init(uint8_t adc_channel);


uint16_t PulseSensor_GetBPM(void);


uint8_t PulseSensor_IsBeat(void);


uint16_t PulseSensor_GetRawSignal(void);

#endif // PULSE_SENSOR_H
