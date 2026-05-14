#include "can_motor.h"
#include <string.h>
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define TAG "CAN"
#define RESP_TIMEOUT_MS 200
#define RX_QUEUE_DEPTH  32

/* Flat struct used to pass frames through the FreeRTOS queue */
typedef struct {
    twai_frame_header_t hdr;
    uint8_t             data[8];
} rx_item_t;

static twai_node_handle_t s_node;
static SemaphoreHandle_t  s_tx_mutex;
static QueueHandle_t      s_rx_queue;

/* ISR callback: read one frame and push it to the queue.
 * IRAM_ATTR required because the TWAI ISR runs from IRAM. */
static bool IRAM_ATTR on_rx_done(twai_node_handle_t node,
                                  const twai_rx_done_event_data_t *edata,
                                  void *user_ctx)
{
    rx_item_t item = {0};
    twai_frame_t frame = {
        .buffer     = item.data,
        .buffer_len = sizeof(item.data),
    };
    BaseType_t woken = pdFALSE;
    if (twai_node_receive_from_isr(node, &frame) == ESP_OK) {
        item.hdr = frame.header;
        xQueueSendFromISR((QueueHandle_t)user_ctx, &item, &woken);
    }
    return woken == pdTRUE;
}

esp_err_t can_motor_init(void)
{
    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_item_t));
    if (!s_rx_queue) return ESP_ERR_NO_MEM;

    twai_onchip_node_config_t cfg = {
        .io_cfg = {
            .tx                = CONFIG_TWAI_TX_GPIO,
            .rx                = CONFIG_TWAI_RX_GPIO,
            .quanta_clk_out    = -1,
            .bus_off_indicator = -1,
        },
        .bit_timing = {
            .bitrate    = 500000,
            .sp_permill = 750,  /* 75% sampling point */
        },
        .tx_queue_depth = 8,
        .fail_retry_cnt = 3,
    };

    ESP_RETURN_ON_ERROR(
        twai_new_node_onchip(&cfg, &s_node),
        TAG, "node create failed");

    twai_event_callbacks_t cbs = { .on_rx_done = on_rx_done };
    ESP_RETURN_ON_ERROR(
        twai_node_register_event_callbacks(s_node, &cbs, s_rx_queue),
        TAG, "register cb failed");

    ESP_RETURN_ON_ERROR(twai_node_enable(s_node), TAG, "enable failed");

    s_tx_mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "CAN ready  TX=GPIO%d  RX=GPIO%d  500 Kbps",
             CONFIG_TWAI_TX_GPIO, CONFIG_TWAI_RX_GPIO);
    return ESP_OK;
}

void can_motor_deinit(void)
{
    if (s_node) {
        twai_node_disable(s_node);
        twai_node_delete(s_node);
        s_node = NULL;
    }
    if (s_rx_queue) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    if (s_tx_mutex) {
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
    }
}

/* MKS checksum: (can_id + cmd + sum(data)) & 0xFF */
static uint8_t mks_crc(uint32_t can_id, uint8_t cmd,
                        const uint8_t *data, uint8_t len)
{
    uint32_t sum = (uint8_t)can_id + cmd;
    for (uint8_t i = 0; i < len; i++) sum += data[i];
    return sum & 0xFF;
}

uint8_t can_motor_cmd(uint32_t can_id, uint8_t cmd,
                      const uint8_t *data, uint8_t data_len,
                      bool wait_resp)
{
    if (!s_node || !s_tx_mutex) return MKS_STATUS_TX_ERR;

    /* Build CAN payload: [cmd][data...][crc] */
    uint8_t payload[8] = {0};
    payload[0] = cmd;
    if (data && data_len) {
        uint8_t n = data_len < 6u ? data_len : 6u;
        memcpy(&payload[1], data, n);
    }
    payload[1 + data_len] = mks_crc(can_id, cmd, data, data_len);

    twai_frame_t tx = {
        .header = {
            .id  = can_id,
            .dlc = 2 + data_len,
            .ide = 0,
            .rtr = 0,
        },
        .buffer     = payload,
        .buffer_len = 2 + data_len,
    };

    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    esp_err_t err = twai_node_transmit(s_node, &tx, 100);
    xSemaphoreGive(s_tx_mutex);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TX err  ID=0x%02X  cmd=0x%02X", (unsigned)can_id, cmd);
        return MKS_STATUS_TX_ERR;
    }

    if (!wait_resp) return MKS_STATUS_OK;

    rx_item_t item;
    if (xQueueReceive(s_rx_queue, &item, pdMS_TO_TICKS(RESP_TIMEOUT_MS)) == pdTRUE) {
        if (item.hdr.dlc >= 2 && item.data[0] == cmd) {
            return item.data[1];
        }
    }
    ESP_LOGW(TAG, "No response  ID=0x%02X  cmd=0x%02X", (unsigned)can_id, cmd);
    return MKS_STATUS_TIMEOUT;
}

esp_err_t can_motor_jog(uint32_t can_id, bool cw,
                         uint16_t speed_rpm, uint8_t accel)
{
    /* F6H Byte2: [dir(b7)][reserved(b6-b4)][speed[11:8](b3-b0)] */
    uint8_t data[3] = {
        (uint8_t)((cw ? 0x80u : 0x00u) | ((speed_rpm >> 8) & 0x0Fu)),
        (uint8_t)(speed_rpm & 0xFFu),
        accel,
    };
    return (can_motor_cmd(can_id, 0xF6, data, 3, false) != MKS_STATUS_TX_ERR)
           ? ESP_OK : ESP_FAIL;
}

void can_motor_drain_rx(void)
{
    rx_item_t item;
    while (xQueueReceive(s_rx_queue, &item, 0) == pdTRUE) {
        /* discard motor responses accumulated during jog */
    }
}
