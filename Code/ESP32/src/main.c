/* ESP32-C3-WROOM-02-N4, center netlist U14:
 * GPIO4 TX -> STM32 PA10, GPIO5 RX <- STM32 PA9; native USB GPIO18/19.
 * USB text commands control a finite read-only test, UART1 carries only V1 frames.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "debug_protocol.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_COUNT 1000u
#define INTERVAL_MS 20u
#define TIMEOUT_MS 300u
#define LINK_UART UART_NUM_1

typedef struct
{
  uint32_t sent, matched, timeouts, bad_status, unexpected_seq, uart_faults, write_errors;
  uint32_t status_frames, stm_errors, stm_drops, stm_errors_start, stm_drops_start;
  uint32_t stm_uptime, resets, latency_max_us, latency_sum_us, started_ms, duration_ms;
  uint8_t active, pending, have_status, result; /* result 0=idle, 1=PASS, 2=FAIL, 3=STOPPED */
  uint16_t sequence;
  int64_t sent_us;
} TestState;
static TestState test;
static FrameParser parser;
static QueueHandle_t uart_events;
static uint16_t serial_sequence;
static uint32_t last_send_ms, split_due_ms;
static uint8_t split_frame[DBG_FRAME_MAX];
static size_t split_length;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void usb_text(const char *s)
{
  (void)usb_serial_jtag_write_bytes(s, strlen(s), pdMS_TO_TICKS(20));
}
static void report(void)
{
  char line[1024];
  const char *state = test.active ? "RUNNING" : test.result == 1 ? "PASS" :
      test.result == 2 ? "FAIL" : test.result == 3 ? "STOPPED" : "READY";
  snprintf(line, sizeof(line),
      "{\"test\":\"esp32_stm32_uart_v1\",\"result\":\"%s\",\"baud\":1000000,"
      "\"tx_gpio\":4,\"rx_gpio\":5,\"target\":%u,\"sent\":%" PRIu32 ","
      "\"matched\":%" PRIu32 ",\"timeouts\":%" PRIu32 ",\"crc_or_frame_errors\":%" PRIu32 ","
      "\"bad_status\":%" PRIu32 ",\"unexpected_seq\":%" PRIu32 ",\"uart_faults\":%" PRIu32 ","
      "\"write_errors\":%" PRIu32 ",\"status_frames\":%" PRIu32 ",\"stm_uart_errors\":%" PRIu32 ","
      "\"stm_tx_drops\":%" PRIu32 ",\"stm_uart_errors_start\":%" PRIu32 ","
      "\"stm_tx_drops_start\":%" PRIu32 ",\"stm_resets\":%" PRIu32 ","
      "\"latency_max_us\":%" PRIu32 ",\"latency_mean_us\":%" PRIu32 ",\"duration_ms\":%" PRIu32 "}\n",
      state, TEST_COUNT, test.sent, test.matched, test.timeouts, parser.errors,
      test.bad_status, test.unexpected_seq, test.uart_faults, test.write_errors,
      test.status_frames, test.stm_errors, test.stm_drops, test.stm_errors_start,
      test.stm_drops_start, test.resets, test.latency_max_us,
      test.matched ? test.latency_sum_us / test.matched : 0, test.duration_ms);
  usb_text(line);
}
static void start_test(void)
{
  if (test.active) { usb_text("BUSY: test already running\n"); return; }
  memset(&test, 0, sizeof(test));
  memset(&parser, 0, sizeof(parser));
  split_length = 0;
  ESP_ERROR_CHECK(uart_flush_input(LINK_UART));
  xQueueReset(uart_events);
  test.active = 1;
  test.started_ms = last_send_ms = now_ms();
  report();
}
static void on_packet(const DebugPacket *p)
{
  if (!test.active) return;
  if (p->type != DBG_STATUS) return; /* Other telemetry has no request result semantics. */
  if (p->len != 140 || p->data[0] != 2 || p->data[2] != 0)
  { test.bad_status++; return; }
  for (unsigned i = 0; i < 5; i++)
    if (p->data[20 + i * 24] != i + 1) { test.bad_status++; return; }
  uint32_t uptime = ReadU32(p->data + 4);
  test.stm_errors = ReadU32(p->data + 8);
  test.stm_drops = ReadU32(p->data + 12);
  if (!test.have_status)
  {
    test.stm_errors_start = test.stm_errors;
    test.stm_drops_start = test.stm_drops;
    test.have_status = 1;
  }
  else if ((int32_t)(uptime - test.stm_uptime) < 0) test.resets++;
  test.stm_uptime = uptime;
  test.status_frames++;
  if (!p->seq) return; /* Periodic status must NOT count as a successful round-trip. */
  if (!test.pending || p->seq != test.sequence)
  { test.unexpected_seq++; return; }
  uint32_t latency = (uint32_t)(esp_timer_get_time() - test.sent_us);
  test.latency_sum_us += latency;
  if (latency > test.latency_max_us) test.latency_max_us = latency;
  test.matched++;
  test.pending = 0;
}
static void link_poll(void)
{
  uart_event_t event;
  while (xQueueReceive(uart_events, &event, 0) == pdTRUE)
  {
    if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
        event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR || event.type == UART_BREAK)
    {
      if (test.active) test.uart_faults++;
      if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL)
      { (void)uart_flush_input(LINK_UART); parser.used = 0; }
    }
  }
  uint8_t bytes[256];
  int count;
  unsigned budget = 8;
  while (budget-- && (count = uart_read_bytes(LINK_UART, bytes, sizeof(bytes), 0)) > 0)
    for (int i = 0; i < count; i++)
    {
      DebugPacket packet;
      if (DebugFeed(&parser, bytes[i], now_ms(), &packet)) on_packet(&packet);
    }
}
static void advance_test(void)
{
  if (!test.active) return;
  uint32_t now = now_ms();
  test.duration_ms = now - test.started_ms;
  if (split_length && (int32_t)(now - split_due_ms) >= 0)
  {
    if (uart_write_bytes(LINK_UART, split_frame + 4, split_length - 4) != (int)(split_length - 4))
      test.write_errors++;
    split_length = 0;
  }
  if (test.pending && (uint32_t)(now - (uint32_t)(test.sent_us / 1000)) >= TIMEOUT_MS)
  { test.timeouts++; test.pending = 0; }
  /* Missing peer fails quickly. A test must not run for five minutes on an unplugged link. */
  if (test.timeouts >= 10 || (test.sent == TEST_COUNT && !test.pending))
  {
    bool pass = test.matched == TEST_COUNT && !test.timeouts && !parser.errors &&
        !test.bad_status && !test.unexpected_seq && !test.uart_faults && !test.write_errors &&
        !test.resets && test.have_status && test.stm_errors == test.stm_errors_start &&
        test.stm_drops == test.stm_drops_start;
    test.active = 0;
    test.result = pass ? 1 : 2;
    report();
    return;
  }
  if (test.pending || now - last_send_ms < INTERVAL_MS) return;
  if (++serial_sequence == 0) serial_sequence = 1;
  test.sequence = serial_sequence;
  const uint8_t command[3] = {CMD_STATUS, 0, 0};
  size_t len = DebugEncode(split_frame, DBG_CMD, test.sequence, command, sizeof(command));
  test.sent_us = esp_timer_get_time();
  test.pending = 1;
  test.sent++;
  last_send_ms = now;
  /* Alternate full writes and a 2 ms split across the header to exercise stream parsing. */
  size_t first = (test.sent & 1u) ? len : 4;
  if (uart_write_bytes(LINK_UART, split_frame, first) != (int)first) test.write_errors++;
  if (first < len) { split_length = len; split_due_ms = now + 2; }
}
static void usb_poll(void)
{
  static char command[32];
  static unsigned used;
  uint8_t bytes[32];
  int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
  for (int i = 0; i < count; i++)
  {
    char c = (char)bytes[i];
    if (c == '\r') continue;
    if (c != '\n')
    { if (used < sizeof(command) - 1) command[used++] = c; continue; }
    command[used] = 0;
    if (!strcmp(command, "run")) start_test();
    else if (!strcmp(command, "status")) report();
    else if (!strcmp(command, "stop"))
    {
      /* Finish an already-started frame so a new run does not inherit a partial frame. */
      if (split_length) (void)uart_write_bytes(LINK_UART, split_frame + 4, split_length - 4);
      split_length = 0; test.active = 0; test.pending = 0; test.result = 3; report();
    }
    else usb_text("Commands: run | status | stop | help\n");
    used = 0;
  }
}
void app_main(void)
{
  esp_log_level_set("*", ESP_LOG_NONE);
  usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 2048, .rx_buffer_size = 256};
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
  uart_config_t uart = {.baud_rate = 1000000, .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
  ESP_ERROR_CHECK(uart_param_config(LINK_UART, &uart));
  ESP_ERROR_CHECK(uart_set_pin(LINK_UART, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(LINK_UART, 4096, 2048, 64, &uart_events, 0));
  usb_text("EnergyMechanism ESP32-C3 UART test; type run to start, help for commands.\n");
  report();
  for (;;)
  {
    link_poll();
    usb_poll();
    advance_test();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
