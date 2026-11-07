/* ESP32-C3 gateway: UART1 GPIO4 TX / GPIO5 RX, 1 Mbps; BLE open bench access. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "gateway_core.h"
#include "usb_gateway.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define EM_UUID(first)                                                                             \
  BLE_UUID128_INIT(0x01, 0x00, 0x4d, 0x45, 0x6d, 0x6c, 0x02, 0x9f, 0x28, 0x4b, 0x3d, 0x7b, first,  \
                   0x00, 0x51, 0xaf)
static const ble_uuid128_t service_uuid = EM_UUID(0);
static const ble_uuid128_t characteristic_uuid[] = {EM_UUID(1), EM_UUID(2), EM_UUID(3), EM_UUID(4),
                                                    EM_UUID(5)};
static Gateway gateway;
static SemaphoreHandle_t gateway_lock;
static QueueHandle_t outbound, uart_events;
static FrameParser parser;
static UsbGateway usb_gateway;
static uint16_t notify_handle[3];
static uint8_t own_address_type;
static struct ble_npl_callout notification_timer;
static void notifications(void);
static const char *tag = "energy_gateway";
static uint32_t now_ms(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000);
}
static int client_index(uint16_t handle)
{
  for (unsigned i = 0; i < V2_MAX_CLIENTS; i++)
    if (gateway.client[i].active && gateway.client[i].handle == handle)
      return (int)i;
  return -1;
}
static bool uart_enqueue(void *ctx, const DebugPacket *p)
{
  (void)ctx;
  return xQueueSend(outbound, p, 0) == pdTRUE;
}
static int access_characteristic(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                                 void *arg)
{
  (void)attr;
  unsigned kind = (unsigned)(uintptr_t)arg;
  if (xSemaphoreTake(gateway_lock, pdMS_TO_TICKS(10)) != pdTRUE)
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  int rc = 0;
  if (kind == 0 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
  {
    uint8_t info[16];
    Gateway_Info(&gateway, info);
    if (os_mbuf_append(ctxt->om, info, sizeof(info)))
      rc = BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  else if (kind == 1 && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
  {
    uint8_t bytes[20];
    uint16_t size = 0;
    int i = client_index(conn);
    if (i < 0 || gateway.client[i].close || !(gateway.client[i].subscribed & 1u))
      rc = BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    else if (ble_hs_mbuf_to_flat(ctxt->om, bytes, sizeof(bytes), &size) || size < 5)
      rc = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    else
      Gateway_Fragment(&gateway, (unsigned)i, bytes, size, now_ms());
  }
  else
    rc = BLE_ATT_ERR_UNLIKELY;
  xSemaphoreGive(gateway_lock);
  return rc;
}
static const struct ble_gatt_chr_def characteristics[] = {{.uuid = &characteristic_uuid[0].u,
                                                           .access_cb = access_characteristic,
                                                           .arg = (void *)(uintptr_t)0,
                                                           .flags = BLE_GATT_CHR_F_READ},
                                                          {.uuid = &characteristic_uuid[1].u,
                                                           .access_cb = access_characteristic,
                                                           .arg = (void *)(uintptr_t)1,
                                                           .flags = BLE_GATT_CHR_F_WRITE},
                                                          {.uuid = &characteristic_uuid[2].u,
                                                           .access_cb = access_characteristic,
                                                           .flags = BLE_GATT_CHR_F_NOTIFY,
                                                           .val_handle = &notify_handle[0]},
                                                          {.uuid = &characteristic_uuid[3].u,
                                                           .access_cb = access_characteristic,
                                                           .flags = BLE_GATT_CHR_F_NOTIFY,
                                                           .val_handle = &notify_handle[1]},
                                                          {.uuid = &characteristic_uuid[4].u,
                                                           .access_cb = access_characteristic,
                                                           .flags = BLE_GATT_CHR_F_NOTIFY,
                                                           .val_handle = &notify_handle[2]},
                                                          {0}};
static const struct ble_gatt_svc_def services[] = {{.type = BLE_GATT_SVC_TYPE_PRIMARY,
                                                    .uuid = &service_uuid.u,
                                                    .characteristics = characteristics},
                                                   {0}};
static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void)
{
  unsigned active = 0;
  xSemaphoreTake(gateway_lock, portMAX_DELAY);
  for (unsigned i = 0; i < V2_MAX_CLIENTS; i++)
    active += gateway.client[i].active ? 1u : 0u;
  xSemaphoreGive(gateway_lock);
  if (active == V2_MAX_CLIENTS || ble_gap_adv_active())
    return;
  struct ble_hs_adv_fields fields = {0}, response = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = (ble_uuid128_t *)&service_uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  const char *name = ble_svc_gap_device_name();
  response.name = (const uint8_t *)name;
  response.name_len = (uint8_t)strlen(name);
  response.name_is_complete = 1;
  int rc = ble_gap_adv_set_fields(&fields);
  if (!rc)
    rc = ble_gap_adv_rsp_set_fields(&response);
  struct ble_gap_adv_params params = {0};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  if (!rc)
    rc = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
  if (rc)
    ESP_LOGW(tag, "advertising failed: %d", rc);
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
  (void)arg;
  switch (event->type)
  {
  case BLE_GAP_EVENT_CONNECT:
    if (!event->connect.status)
    {
      xSemaphoreTake(gateway_lock, portMAX_DELAY);
      int i = Gateway_Connect(&gateway, event->connect.conn_handle);
      xSemaphoreGive(gateway_lock);
      if (i < 0)
        (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    advertise();
    break;
  case BLE_GAP_EVENT_DISCONNECT:
  {
    xSemaphoreTake(gateway_lock, portMAX_DELAY);
    int i = client_index(event->disconnect.conn.conn_handle);
    if (i >= 0)
      Gateway_Disconnect(&gateway, (unsigned)i);
    xSemaphoreGive(gateway_lock);
    advertise();
    break;
  }
  case BLE_GAP_EVENT_SUBSCRIBE:
  {
    xSemaphoreTake(gateway_lock, portMAX_DELAY);
    int i = client_index(event->subscribe.conn_handle);
    if (i >= 0)
      for (unsigned channel = 0; channel < 3; channel++)
        if (event->subscribe.attr_handle == notify_handle[channel])
          Gateway_Subscribe(&gateway, (unsigned)i, (uint8_t)channel,
                            event->subscribe.cur_notify != 0);
    xSemaphoreGive(gateway_lock);
    break;
  }
  case BLE_GAP_EVENT_ADV_COMPLETE:
    advertise();
    break;
  default:
    break;
  }
  return 0;
}
static void on_reset(int reason)
{
  ESP_LOGW(tag, "BLE host reset: %d", reason);
  ble_npl_callout_stop(&notification_timer);
  xSemaphoreTake(gateway_lock, portMAX_DELAY);
  for (unsigned i = 0; i < V2_MAX_CLIENTS; i++)
    Gateway_Disconnect(&gateway, i);
  xSemaphoreGive(gateway_lock);
}
static void on_sync(void)
{
  int rc = ble_hs_util_ensure_addr(0);
  if (!rc)
    rc = ble_hs_id_infer_auto(0, &own_address_type);
  if (rc)
    ESP_LOGE(tag, "BLE address failed: %d", rc);
  else
  {
    advertise();
    (void)ble_npl_callout_reset(&notification_timer, ble_npl_time_ms_to_ticks32(5));
  }
}
static void host_task(void *arg)
{
  (void)arg;
  nimble_port_run();
  nimble_port_freertos_deinit();
}
static void uart_poll(void)
{
  uart_event_t event;
  unsigned budget = 8;
  while (budget-- && xQueueReceive(uart_events, &event, 0) == pdTRUE)
  {
    if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
        event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR)
    {
      (void)uart_flush_input(UART_NUM_1);
      parser.used = 0;
      xSemaphoreTake(gateway_lock, portMAX_DELAY);
      gateway.frame_errors++;
      xSemaphoreGive(gateway_lock);
    }
  }
  uint8_t bytes[512];
  int size = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), 0);
  DebugPacket p;
  uint32_t now = now_ms();
  for (int i = 0; i < size; i++)
    if (WireFeed(&parser, bytes[i], now, &p, true))
    {
      xSemaphoreTake(gateway_lock, portMAX_DELAY);
      Gateway_Uart(&gateway, &p, now);
      xSemaphoreGive(gateway_lock);
    }
  /* No blocking UART writes in the BLE callback or in gateway state transitions. */
  static uint8_t tx[DBG_FRAME_MAX];
  static size_t used, sent;
  if (sent == used && xQueueReceive(outbound, &p, 0) == pdTRUE)
  {
    used = WireEncode(tx, p.version, p.type, p.seq, p.data, p.len);
    sent = 0;
  }
  if (sent < used)
  {
    int count = uart_tx_chars(UART_NUM_1, (const char *)tx + sent, (uint32_t)(used - sent));
    if (count > 0)
      sent += (size_t)count;
  }
}
static void notifications(void)
{
  /* One fragment per connection/channel per turn; slow peers retain only latest pressure. */
  xSemaphoreTake(gateway_lock, portMAX_DELAY);
  for (unsigned i = 0; i < V2_MAX_CLIENTS; i++)
  {
    GatewayClient *c = &gateway.client[i];
    if (!c->active)
      continue;
    if (c->close)
    {
      (void)ble_gap_terminate(c->handle, BLE_ERR_REM_USER_CONN_TERM);
      continue;
    }
    for (uint8_t channel = 0; channel < 3; channel++)
    {
      uint8_t bytes[20];
      size_t n = Gateway_NextFragment(&gateway, i, channel, bytes);
      if (!n)
        continue;
      struct os_mbuf *om = ble_hs_mbuf_from_flat(bytes, (uint16_t)n);
      if (!om)
        break;
      int rc = ble_gatts_notify_custom(c->handle, notify_handle[channel], om);
      /* NimBLE consumes om on both success and failure. */
      if (!rc)
        Gateway_FragmentSent(&gateway, i, channel);
      else
        break;
    }
  }
  xSemaphoreGive(gateway_lock);
}
static void notification_event(struct ble_npl_event *event)
{
  (void)event;
  /* BLE calls run on the host queue, serialized with disconnect/reconnect callbacks. */
  notifications();
  (void)ble_npl_callout_reset(&notification_timer, ble_npl_time_ms_to_ticks32(5));
}
static void usb_poll(void)
{
  uint8_t input[512];
  const uint8_t *output = NULL;
  int n = usb_serial_jtag_read_bytes(input, sizeof(input), 0);
  uint32_t now = now_ms();
  xSemaphoreTake(gateway_lock, portMAX_DELAY);
  for (int i = 0; i < n; i++) UsbGateway_Feed(&usb_gateway, &gateway, input[i], now);
  UsbGateway_Poll(&usb_gateway, &gateway, now);
  size_t size = UsbGateway_Next(&usb_gateway, &gateway, &output);
  xSemaphoreGive(gateway_lock);
  /* No wait for a USB host and no USB driver calls while holding gateway_lock. */
  if (size) {
    int sent = usb_serial_jtag_write_bytes(output, size, 0);
    if (sent > 0) UsbGateway_Sent(&usb_gateway, (size_t)sent);
  }
}
void app_main(void)
{
  /* USB is binary from this point. ROM/bootloader text is tolerated by stream parsers. */
  esp_log_level_set("*", ESP_LOG_NONE);
  usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 1024, .rx_buffer_size = 2048};
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
  UsbGateway_Init(&usb_gateway);
  ESP_ERROR_CHECK(
      nvs_flash_init()); /* Do not erase NVS automatically on an initialization error. */
  gateway_lock = xSemaphoreCreateMutex();
  outbound = xQueueCreate(8, sizeof(DebugPacket));
  if (!gateway_lock || !outbound)
    abort();
  uart_config_t uart = {.baud_rate = 1000000,
                        .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                        .source_clk = UART_SCLK_DEFAULT};
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 4096, 0, 16, &uart_events, 0));
  ESP_ERROR_CHECK(nimble_port_init());
  uint8_t device[12] = {0};
  uint64_t epoch;
  ESP_ERROR_CHECK(esp_efuse_mac_get_default(device));
  esp_fill_random(&epoch, sizeof(epoch));
  Gateway_Init(&gateway, epoch, device, (GatewayBackend){uart_enqueue, NULL});
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ESP_ERROR_CHECK(ble_gatts_count_cfg(services));
  ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
  char name[32];
  snprintf(name, sizeof(name), "EnergyMechanism-%02X%02X", device[4], device[5]);
  ESP_ERROR_CHECK(ble_svc_gap_device_name_set(name));
  ble_npl_callout_init(&notification_timer, nimble_port_get_dflt_eventq(), notification_event,
                       NULL);
  nimble_port_freertos_init(host_task);
  for (;;)
  {
    uart_poll();
    usb_poll();
    xSemaphoreTake(gateway_lock, portMAX_DELAY);
    Gateway_Poll(&gateway, now_ms());
    xSemaphoreGive(gateway_lock);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
