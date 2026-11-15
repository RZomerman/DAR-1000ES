#include "sony_uart_link.h"
#include "esp_log.h"
#include "esp_system.h"

void app_main(void) {
  ESP_LOGI("sony_audio_bench", "Boot reset reason: %d", esp_reset_reason());
  ESP_ERROR_CHECK(sony_uart_link_init());
  ESP_LOGI("sony_audio_bench", "UART TX43/RX44, 115200 8N1; chip4 /OE HIGH");
}
