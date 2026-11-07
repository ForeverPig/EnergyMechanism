#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "hit_detector.h"

void App_Init(void);
void App_Process(void);
void App_UsbTransmitComplete(void);
void App_GetSnapshots(HitSensorSnapshot out[HIT_SENSOR_COUNT]);
uint32_t App_GetDmaDropCount(void);
uint32_t App_GetAdcErrorCount(void);
uint32_t App_GetDebugDropCount(void);
uint8_t App_GetCalibrationStatus(void);
/* LED setters are non-blocking and should be called from application context. */
uint8_t App_SetRingColor(uint8_t ring, uint8_t red, uint8_t green, uint8_t blue);
uint8_t App_SetRingBrightness(uint8_t ring, uint8_t brightness);
void App_SetGlobalBrightness(uint8_t brightness);
void App_OnHitEvent(const HitEvent *event);

#endif
