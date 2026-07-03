#include "bk_hiwonder_car.h"

#include <components/log.h>
#include <driver/uart.h>
#include <os/mem.h>
#include <os/os.h>

#include <stdbool.h>
#include <stdint.h>

#define TAG "hiwonder-car"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* v1.2 uses UART4 as the Hiwonder packet link at 1000000 8N1. */
#define BK_HIWONDER_CAR_UART_ID          UART_ID_5
#define BK_HIWONDER_CAR_UART_BAUD_RATE   1000000
#define BK_HIWONDER_CAR_UART_TASK_STACK  3072
#define BK_HIWONDER_CAR_FRAME_MAX        260

#define HIWONDER_FRAME_HEAD_0            0xAA
#define HIWONDER_FRAME_HEAD_1            0x55
#define HIWONDER_FUNC_PWM_SERVO          0x04
#define HIWONDER_FUNC_CAR_CHASSIS        0x0A
#define HIWONDER_PWM_SERVO_SET_ONE       0x03

static beken_thread_t s_uart_rx_thread;
static volatile bool s_uart_rx_running;
static bool s_uart_inited;

static uint8_t hiwonder_crc8(const uint8_t *buf, uint16_t len)
{
    uint8_t crc = 0;

    while (len--) {
        crc ^= *buf++;
        for (uint8_t i = 0; i < 8; ++i) {
            crc = (crc & 0x01) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
        }
    }

    return crc;
}

bk_err_t bk_hiwonder_car_output(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > BK_HIWONDER_CAR_FRAME_MAX) {
        return BK_ERR_PARAM;
    }
    if (!s_uart_inited) {
        LOGE("UART5 not initialized\r\n");
        return BK_FAIL;
    }

    return bk_uart_write_bytes(BK_HIWONDER_CAR_UART_ID, data, len);
}

static bk_err_t hiwonder_car_send_frame(uint8_t func, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t frame[BK_HIWONDER_CAR_FRAME_MAX];
    uint16_t frame_len = (uint16_t)payload_len + 5U;

    frame[0] = HIWONDER_FRAME_HEAD_0;
    frame[1] = HIWONDER_FRAME_HEAD_1;
    frame[2] = func;
    frame[3] = payload_len;
    if (payload_len > 0 && payload != NULL) {
        os_memcpy(&frame[4], payload, payload_len);
    }
    frame[4 + payload_len] = hiwonder_crc8(&frame[2], (uint16_t)payload_len + 2U);

    return bk_hiwonder_car_output(frame, frame_len);
}

static void hiwonder_car_handle_rx_frame(const uint8_t *frame, uint16_t frame_len)
{
    uint8_t payload_len = frame[3];
    uint8_t crc = hiwonder_crc8(&frame[2], (uint16_t)payload_len + 2U);

    if (crc != frame[4 + payload_len]) {
        LOGW("rx crc mismatch func=0x%02X len=%u calc=0x%02X recv=0x%02X\r\n",
             frame[2], payload_len, crc, frame[4 + payload_len]);
        return;
    }
}

static void hiwonder_car_uart_rx_task(void *arg)
{
    enum {
        RX_WAIT_HEAD0,
        RX_WAIT_HEAD1,
        RX_WAIT_FUNC,
        RX_WAIT_LEN,
        RX_WAIT_PAYLOAD,
        RX_WAIT_CRC,
    } state = RX_WAIT_HEAD0;
    uint8_t frame[BK_HIWONDER_CAR_FRAME_MAX];
    uint8_t payload_len = 0;
    uint16_t index = 0;

    (void)arg;

    while (s_uart_rx_running) {
        uint8_t byte = 0;
        int len = bk_uart_read_bytes(BK_HIWONDER_CAR_UART_ID, &byte, 1, BEKEN_WAIT_FOREVER);
        if (len <= 0) {
            continue;
        }

        switch (state) {
        case RX_WAIT_HEAD0:
            if (byte == HIWONDER_FRAME_HEAD_0) {
                frame[0] = byte;
                index = 1;
                state = RX_WAIT_HEAD1;
            }
            break;
        case RX_WAIT_HEAD1:
            if (byte == HIWONDER_FRAME_HEAD_1) {
                frame[index++] = byte;
                state = RX_WAIT_FUNC;
            } else {
                state = RX_WAIT_HEAD0;
            }
            break;
        case RX_WAIT_FUNC:
            frame[index++] = byte;
            state = RX_WAIT_LEN;
            break;
        case RX_WAIT_LEN:
            payload_len = byte;
            frame[index++] = byte;
            state = (payload_len == 0) ? RX_WAIT_CRC : RX_WAIT_PAYLOAD;
            break;
        case RX_WAIT_PAYLOAD:
            frame[index++] = byte;
            if (index >= (uint16_t)payload_len + 4U) {
                state = RX_WAIT_CRC;
            }
            break;
        case RX_WAIT_CRC:
            frame[index++] = byte;
            hiwonder_car_handle_rx_frame(frame, index);
            state = RX_WAIT_HEAD0;
            index = 0;
            break;
        default:
            state = RX_WAIT_HEAD0;
            index = 0;
            break;
        }
    }

    s_uart_rx_thread = NULL;
    rtos_delete_thread(NULL);
}

bk_err_t bk_hiwonder_car_init(void)
{
    if (s_uart_inited) {
        return BK_OK;
    }

    uart_config_t config = {
        .baud_rate = BK_HIWONDER_CAR_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_NONE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_FLOWCTRL_DISABLE,
        .src_clk = UART_SCLK_APLL,
        .rx_dma_en = UART_DMA_DISABLE,
        .tx_dma_en = UART_DMA_DISABLE,
    };

    bk_err_t ret = bk_uart_init(BK_HIWONDER_CAR_UART_ID, &config);
    if (ret != BK_OK) {
        LOGE("UART5 init failed: %d\r\n", ret);
        return ret;
    }

    s_uart_rx_running = true;
    ret = rtos_create_thread(&s_uart_rx_thread,
                             BEKEN_DEFAULT_WORKER_PRIORITY,
                             "uart5_car",
                             (beken_thread_function_t)hiwonder_car_uart_rx_task,
                             BK_HIWONDER_CAR_UART_TASK_STACK,
                             NULL);
    if (ret != BK_OK) {
        s_uart_rx_running = false;
        bk_uart_deinit(BK_HIWONDER_CAR_UART_ID);
        LOGE("UART5 rx task create failed: %d\r\n", ret);
        return ret;
    }

    s_uart_inited = true;
    LOGI("UART5 car link initialized: TX GPIO65, RX GPIO64, %u 8N1\r\n",
         BK_HIWONDER_CAR_UART_BAUD_RATE);
    return BK_OK;
}

bk_err_t bk_hiwonder_car_run(float vx_mm_s, float angular_rate)
{
    uint8_t payload[8];

    if (bk_hiwonder_car_init() != BK_OK) {
        return BK_FAIL;
    }

    os_memcpy(&payload[0], &vx_mm_s, sizeof(vx_mm_s));
    os_memcpy(&payload[4], &angular_rate, sizeof(angular_rate));

    return hiwonder_car_send_frame(HIWONDER_FUNC_CAR_CHASSIS, payload, sizeof(payload));
}

bk_err_t bk_hiwonder_car_set_pwm_servo(uint8_t servo_id,
                                       uint16_t pulse,
                                       uint16_t duration_ms)
{
    uint8_t payload[6];

    if (bk_hiwonder_car_init() != BK_OK) {
        return BK_FAIL;
    }

    payload[0] = HIWONDER_PWM_SERVO_SET_ONE;
    payload[1] = (uint8_t)(duration_ms & 0xFF);
    payload[2] = (uint8_t)(duration_ms >> 8);
    payload[3] = servo_id;
    payload[4] = (uint8_t)(pulse & 0xFF);
    payload[5] = (uint8_t)(pulse >> 8);

    return hiwonder_car_send_frame(HIWONDER_FUNC_PWM_SERVO, payload, sizeof(payload));
}

bk_err_t bk_hiwonder_car_send_raw(const uint8_t *data, uint16_t len)
{
    if (bk_hiwonder_car_init() != BK_OK) {
        return BK_FAIL;
    }

    return bk_hiwonder_car_output(data, len);
}
