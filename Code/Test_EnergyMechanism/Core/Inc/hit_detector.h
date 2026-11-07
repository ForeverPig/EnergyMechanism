#ifndef HIT_DETECTOR_H
#define HIT_DETECTOR_H

#include <stdint.h>

#define HIT_SENSOR_COUNT 10u

typedef enum {
  HIT_STATE_CALIBRATING = 0,
  HIT_STATE_IDLE,
  HIT_STATE_CANDIDATE,
  HIT_STATE_WAIT_RELEASE,
  HIT_STATE_LOCKED
} HitDetectorState;

typedef enum {
  HIT_CALIBRATION_RUNNING = 0,
  HIT_CALIBRATION_READY,
  HIT_CALIBRATION_UNSTABLE
} HitCalibrationStatus;

typedef struct {
  uint8_t ring;
  uint16_t peak;
  uint32_t timestamp_ms;
} HitEvent;

typedef struct {
  uint16_t raw;
  int32_t baseline;
  int16_t delta;
  uint16_t peak;
  uint8_t state;
} HitSensorSnapshot;

typedef struct {
  uint32_t calibration_ms;
  uint16_t calibration_max_range;
  uint16_t start_delta;
  uint16_t min_peak;
  uint16_t rise_delta;
  uint16_t release_delta;
  uint16_t min_width_samples;
  uint16_t max_width_samples;
  uint16_t release_stable_samples;
  uint16_t group_samples;
  uint16_t lockout_samples;
  uint8_t baseline_track_shift;
} HitDetectorConfig;

typedef struct {
  uint16_t raw[HIT_SENSOR_COUNT];
  uint16_t filtered[HIT_SENSOR_COUNT][3];
  uint8_t filter_index[HIT_SENSOR_COUNT];
  uint8_t filter_count[HIT_SENSOR_COUNT];
  int32_t baseline[HIT_SENSOR_COUNT];
  int32_t baseline_track_accum[HIT_SENSOR_COUNT];
  int16_t delta[HIT_SENSOR_COUNT];
  uint16_t peak[HIT_SENSOR_COUNT];
  int16_t delta_history[HIT_SENSOR_COUNT][2];
  uint16_t calibration_min[HIT_SENSOR_COUNT];
  uint16_t calibration_max[HIT_SENSOR_COUNT];
  uint32_t calibration_sum[HIT_SENSOR_COUNT];
  uint32_t state_start[HIT_SENSOR_COUNT];
  uint32_t onset_sample[HIT_SENSOR_COUNT];
  int16_t onset_delta[HIT_SENSOR_COUNT];
  uint32_t release_start[HIT_SENSOR_COUNT];
  uint32_t group_start;
  uint32_t lockout_until;
  uint32_t group_release_start;
  uint32_t sample_index;
  uint16_t calibration_samples;
  uint8_t calibration_status;
  uint8_t state[HIT_SENSOR_COUNT];
  uint8_t group_active;
  uint8_t group_valid;
  uint8_t locked;
  uint8_t group_ring;
  uint16_t group_peak;
  uint32_t group_timestamp;
} HitDetector;

void HitDetector_DefaultConfig(HitDetectorConfig *config);
void HitDetector_Init(HitDetector *detector);
void HitDetector_Reset(HitDetector *detector);
void HitDetector_Process(HitDetector *detector,
                         const HitDetectorConfig *config,
                         const uint16_t raw[HIT_SENSOR_COUNT],
                         uint32_t timestamp_ms,
                         HitEvent *event,
                         uint8_t *event_ready);
void HitDetector_GetSnapshot(const HitDetector *detector,
                             HitSensorSnapshot out[HIT_SENSOR_COUNT]);
uint8_t HitDetector_GetCalibrationStatus(const HitDetector *detector);

#endif
