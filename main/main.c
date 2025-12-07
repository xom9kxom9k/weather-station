#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"

#include "web_page.h"

#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_PORT I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000

#define BME280_ADDR 0x76

#define BME280_REG_ID 0xD0
#define BME280_REG_RESET 0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS 0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_PRESS_MSB 0xF7

#include "wifi_config.h"

#define MAX_STA_CONN 1

static const char *TAG = "weather_station";

static httpd_handle_t s_httpd_handle = NULL;
static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static double s_last_temperature = 0.0;
static double s_last_pressure = 0.0;
static double s_last_humidity = 0.0;

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_data_t;

static bme280_calib_data_t s_calib;
static int32_t s_t_fine = 0;

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &conf));
    return i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t bme280_write(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(
        I2C_MASTER_PORT, BME280_ADDR, data, sizeof(data),
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t bme280_read(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(
        I2C_MASTER_PORT,
        BME280_ADDR,
        &reg,
        1,
        data,
        len,
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t bme280_read_calibration_data(void) {
    uint8_t buf[26];
    ESP_RETURN_ON_ERROR(bme280_read(0x88, buf, 26), TAG, "Failed to read calib T/P");

    s_calib.dig_T1 = (uint16_t)buf[1] << 8 | buf[0];
    s_calib.dig_T2 = (int16_t)buf[3] << 8 | buf[2];
    s_calib.dig_T3 = (int16_t)buf[5] << 8 | buf[4];

    s_calib.dig_P1 = (uint16_t)buf[7] << 8 | buf[6];
    s_calib.dig_P2 = (int16_t)buf[9] << 8 | buf[8];
    s_calib.dig_P3 = (int16_t)buf[11] << 8 | buf[10];
    s_calib.dig_P4 = (int16_t)buf[13] << 8 | buf[12];
    s_calib.dig_P5 = (int16_t)buf[15] << 8 | buf[14];
    s_calib.dig_P6 = (int16_t)buf[17] << 8 | buf[16];
    s_calib.dig_P7 = (int16_t)buf[19] << 8 | buf[18];
    s_calib.dig_P8 = (int16_t)buf[21] << 8 | buf[20];
    s_calib.dig_P9 = (int16_t)buf[23] << 8 | buf[22];

    s_calib.dig_H1 = buf[25];

    uint8_t buf_h[7];
    ESP_RETURN_ON_ERROR(bme280_read(0xE1, buf_h, 7), TAG, "Failed to read calib H");

    s_calib.dig_H2 = (int16_t)buf_h[1] << 8 | buf_h[0];
    s_calib.dig_H3 = buf_h[2];
    s_calib.dig_H4 = (int16_t)((buf_h[3] << 4) | (buf_h[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)((buf_h[5] << 4) | (buf_h[4] >> 4));
    s_calib.dig_H6 = (int8_t)buf_h[6];

    return ESP_OK;
}

static esp_err_t bme280_init(void) {
    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(bme280_read(BME280_REG_ID, &chip_id, 1), TAG, "Unable to read chip ID");
    if (chip_id != 0x60) {
        ESP_LOGE(TAG, "Unexpected chip id: 0x%02X", chip_id);
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(bme280_write(BME280_REG_RESET, 0xB6), TAG, "Failed to reset");
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_RETURN_ON_ERROR(bme280_read_calibration_data(), TAG, "Calibration data error");

    ESP_RETURN_ON_ERROR(bme280_write(BME280_REG_CTRL_HUM, 0x01), TAG, "ctrl_hum");
    ESP_RETURN_ON_ERROR(bme280_write(BME280_REG_CONFIG, 0x14), TAG, "config");
    ESP_RETURN_ON_ERROR(bme280_write(BME280_REG_CTRL_MEAS, 0x27), TAG, "ctrl_meas");

    return ESP_OK;
}

static double compensate_temperature(int32_t adc_T) {
    double var1 =
        (((double)adc_T) / 16384.0 - ((double)s_calib.dig_T1) / 1024.0) * ((double)s_calib.dig_T2);
    double var2 =
        ((((double)adc_T) / 131072.0 - ((double)s_calib.dig_T1) / 8192.0) *
         (((double)adc_T) / 131072.0 - ((double)s_calib.dig_T1) / 8192.0)) *
        ((double)s_calib.dig_T3);
    s_t_fine = (int32_t)(var1 + var2);
    return (var1 + var2) / 5120.0;
}

static double compensate_pressure(int32_t adc_P) {
    double var1 = ((double)s_t_fine / 2.0) - 64000.0;
    double var2 = var1 * var1 * ((double)s_calib.dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)s_calib.dig_P5) * 2.0;
    var2 = (var2 / 4.0) + (((double)s_calib.dig_P4) * 65536.0);
    var1 = (((double)s_calib.dig_P3) * var1 * var1 / 524288.0 + ((double)s_calib.dig_P2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)s_calib.dig_P1);
    if (var1 == 0.0) {
        return 0;
    }
    double pressure = 1048576.0 - (double)adc_P;
    pressure = (pressure - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)s_calib.dig_P9) * pressure * pressure / 2147483648.0;
    var2 = pressure * ((double)s_calib.dig_P8) / 32768.0;
    pressure = pressure + (var1 + var2 + ((double)s_calib.dig_P7)) / 16.0;
    return pressure / 100.0;
}

static double compensate_humidity(int32_t adc_H) {
    double var_H = ((double)s_t_fine) - 76800.0;
    var_H = (adc_H - (((double)s_calib.dig_H4) * 64.0 + ((double)s_calib.dig_H5) / 16384.0 * var_H)) *
            (((double)s_calib.dig_H2) / 65536.0 *
             (1.0 + ((double)s_calib.dig_H6) / 67108864.0 * var_H *
                        (1.0 + ((double)s_calib.dig_H3) / 67108864.0 * var_H)));
    var_H = var_H * (1.0 - ((double)s_calib.dig_H1) * var_H / 524288.0);
    if (var_H > 100.0) {
        var_H = 100.0;
    } else if (var_H < 0.0) {
        var_H = 0.0;
    }
    return var_H;
}

static esp_err_t bme280_read_measurements(double *temperature, double *pressure, double *humidity) {
    uint8_t data[8];
    ESP_RETURN_ON_ERROR(bme280_read(BME280_REG_PRESS_MSB, data, sizeof(data)), TAG, "Read raw data");

    int32_t adc_P = (int32_t)(((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | (data[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)data[6] << 8) | data[7]);

    *temperature = compensate_temperature(adc_T);
    *pressure = compensate_pressure(adc_P);
    *humidity = compensate_humidity(adc_H);

    // Обновляем глобальные переменные
    s_last_temperature = *temperature;
    s_last_pressure = *pressure;
    s_last_humidity = *humidity;

    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Retry connecting to WiFi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);
}

static esp_err_t http_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_api_data_handler(httpd_req_t *req) {
    char response[200];
    snprintf(response, sizeof(response),
             "{\"temperature\":%.2f,\"pressure\":%.2f,\"humidity\":%.2f}",
             s_last_temperature, s_last_pressure, s_last_humidity);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_get_handler,
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t api_uri = {
            .uri = "/api/data",
            .method = HTTP_GET,
            .handler = http_api_data_handler,
        };
        httpd_register_uri_handler(server, &api_uri);

        ESP_LOGI(TAG, "HTTP server started on port 80");
        return server;
    }

    ESP_LOGE(TAG, "Error starting HTTP server");
    return NULL;
}

static void stop_webserver(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(i2c_master_init());
    ESP_ERROR_CHECK(bme280_init());

    wifi_init_sta();

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        false, false, portMAX_DELAY);

    s_httpd_handle = start_webserver();

    while (true) {
        double temperature = 0, pressure = 0, humidity = 0;
        if (bme280_read_measurements(&temperature, &pressure, &humidity) == ESP_OK) {
            ESP_LOGI(TAG, "Temperature: %.2f °C | Pressure: %.2f hPa | Humidity: %.2f %%", 
                     temperature, pressure, humidity);
        } else {
            ESP_LOGW(TAG, "Failed to read BME280 data");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
