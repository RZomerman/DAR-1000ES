#include "radio.h"
#include "settings.h"
#include "sony_keypad.h"
#include "sony_ota.h"
#include "sony_controller_menu.h"
#include "sony_controller_radio.h"
#include "sony_screen.h"
#include "sony_uart_link.h"
#include "sony_vfd.h"
#include "wifi.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static esp_err_t root(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_sendstr(req,
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
      "<title>DAR-1000ES controller settings</title><h1>Controller settings</h1>"
      "<form id=settings><h2>Wi-Fi</h2>"
      "<p><label>Network name <input id=ssid required maxlength=32></label></p>"
      "<p><label>Password <input id=password type=password maxlength=64></label></p>"
      "<button>Save Wi-Fi settings</button></form><p id=result role=status></p>"
      "<h2>Physical presets 1–16</h2><p>Use a direct MP3 or AAC stream URL. "
      "Clear both fields and save to unassign a key.</p><div id=presets></div>"
      "<p id=presetStatus role=status></p>"
      "<script>document.getElementById('settings').onsubmit=async e=>{"
      "e.preventDefault();const p=document.getElementById('password'),"
      "out=document.getElementById('result');"
      "try{const r=await fetch('/api/wifi/config',{method:'POST',"
      "headers:{'Content-Type':'application/json'},body:JSON.stringify({"
      "ssid:document.getElementById('ssid').value,password:p.value})});"
      "out.textContent=await r.text();if(r.ok)p.value='';"
      "}catch{out.textContent='Could not save Wi-Fi settings.'}};"
      "const list=document.getElementById('presets'),presetStatus=document.getElementById('presetStatus');"
      "async function loadPresets(){try{const r=await fetch('/api/radio/presets',{cache:'no-store'});"
      "if(!r.ok)throw Error(await r.text());for(const p of await r.json()){"
      "const f=document.createElement('form');"
      "f.innerHTML='<p><strong>Key '+p.number+'</strong> <label>Station <input maxlength=63></label> "
      "<label>Stream URL <input maxlength=511 type=url></label> <button>Save</button></p>';"
      "const [name,url]=f.querySelectorAll('input'),button=f.querySelector('button');"
      "name.value=p.name;url.value=p.url;f.onsubmit=async e=>{e.preventDefault();button.disabled=true;"
      "try{const r=await fetch('/api/radio/presets',{method:'POST',"
      "headers:{'Content-Type':'application/json'},body:JSON.stringify({number:p.number,"
      "name:name.value.trim(),url:url.value.trim()})});"
      "if(!r.ok)throw Error(await r.text());presetStatus.textContent='Key '+p.number+' saved.';"
      "}catch(e){presetStatus.textContent=e.message}finally{button.disabled=false}};list.append(f);}"
      "}catch(e){presetStatus.textContent=e.message}}loadPresets();</script>");
}

static esp_err_t get_presets(httpd_req_t *req) {
  cJSON *rows = cJSON_CreateArray();
  if (!rows)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory.");
  for (unsigned number = 1; number <= 16; ++number) {
    sony_station_command_t station = {0};
    esp_err_t err = sony_controller_get_preset(number, &station);
    cJSON *row = cJSON_CreateObject();
    if (!row || (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) ||
        !cJSON_AddNumberToObject(row, "number", number) ||
        !cJSON_AddStringToObject(row, "name", err == ESP_OK ? station.name : "") ||
        !cJSON_AddStringToObject(row, "url", err == ESP_OK ? station.url : "") ||
        !cJSON_AddItemToArray(rows, row)) {
      cJSON_Delete(row);
      cJSON_Delete(rows);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot read presets.");
    }
  }
  char *json = cJSON_PrintUnformatted(rows);
  cJSON_Delete(rows);
  if (!json)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory.");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = httpd_resp_sendstr(req, json);
  cJSON_free(json);
  return err;
}

static esp_err_t save_preset(httpd_req_t *req) {
  char type[32], body[1024];
  if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      strcmp(type, "application/json") || !req->content_len || req->content_len >= sizeof(body))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a preset JSON object.");
  size_t used = 0;
  while (used < req->content_len) {
    int n = httpd_req_recv(req, body + used, req->content_len - used);
    if (n <= 0)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request.");
    used += n;
  }
  body[used] = 0;
  cJSON *root = cJSON_ParseWithLengthOpts(body, used + 1, NULL, true);
  cJSON *number = cJSON_GetObjectItemCaseSensitive(root, "number");
  const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "name"));
  const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "url"));
  sony_station_command_t station = {.generation = 1};
  esp_err_t err = ESP_ERR_INVALID_ARG;
  if (cJSON_IsNumber(number) && number->valueint == number->valuedouble &&
      number->valueint >= 1 && number->valueint <= 16 && name && url &&
      strlen(name) < sizeof(station.name) && strlen(url) < sizeof(station.url)) {
    if (!*name && !*url) {
      err = sony_controller_clear_preset(number->valueint);
    } else {
      if (sony_controller_get_preset(number->valueint, &station) != ESP_OK ||
          strcmp(station.name, name) || strcmp(station.url, url)) {
        memset(&station, 0, sizeof(station));
        station.generation = 1;
      }
      strcpy(station.name, name);
      strcpy(station.url, url);
      err = sony_controller_save_preset(number->valueint, &station);
    }
  }
  cJSON_Delete(root);
  if (err == ESP_ERR_INVALID_ARG)
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Enter a name and direct stream URL, or clear both.");
  if (err != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot save preset.");
  return httpd_resp_sendstr(req, "Saved.");
}

static esp_err_t save_wifi(httpd_req_t *req) {
  char type[32], body[640];
  if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      strcmp(type, "application/json"))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected JSON.");
  if (!req->content_len || req->content_len >= sizeof(body))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size.");
  size_t used = 0;
  while (used < req->content_len) {
    int n = httpd_req_recv(req, body + used, req->content_len - used);
    if (n <= 0)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request.");
    used += n;
  }
  body[used] = 0;
  cJSON *root = cJSON_ParseWithLengthOpts(body, used + 1, NULL, true);
  const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "ssid"));
  const char *password = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "password"));
  esp_err_t err = settings_set_wifi_credentials(ssid, password);
  cJSON_Delete(root);
  if (err != ESP_OK)
    return httpd_resp_send_err(req, err == ESP_ERR_INVALID_ARG ?
        HTTPD_400_BAD_REQUEST : HTTPD_500_INTERNAL_SERVER_ERROR,
        err == ESP_ERR_INVALID_ARG ? "Invalid network name or password." :
                                     "Could not save Wi-Fi settings.");
  return httpd_resp_sendstr(req, "Saved. The new Wi-Fi settings apply at the next power cycle.");
}

static esp_err_t info(httpd_req_t *req) {
  char result[128];
  snprintf(result, sizeof(result),
           "{\"info\":{\"firmware_version\":\"%s\",\"role\":\"controller\"},\"success\":true}",
           esp_app_get_description()->version);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, result);
}

static void start_http(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 6;
  config.stack_size = 8192;
  httpd_handle_t server = NULL;
  ESP_ERROR_CHECK(httpd_start(&server, &config));
  const httpd_uri_t paths[] = {
      {.uri = "/", .method = HTTP_GET, .handler = root},
      {.uri = "/api/system/info", .method = HTTP_GET, .handler = info},
      {.uri = "/api/radio/presets", .method = HTTP_GET, .handler = get_presets},
      {.uri = "/api/radio/presets", .method = HTTP_POST, .handler = save_preset},
      {.uri = "/api/wifi/config", .method = HTTP_POST, .handler = save_wifi},
      {.uri = "/api/ota/update", .method = HTTP_POST, .handler = sony_ota_update},
  };
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &paths[i]));
}

static void audio_display_task(void *unused) {
  (void)unused;
  sony_display_mode_t last_mode = SONY_DISPLAY_MODE_COUNT;
  uint32_t last_generation = 0;
  int64_t next_text = 0;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    sony_audio_link_status_t audio;
    sony_uart_link_audio_status(&audio);
    sony_screen_board_indicators(audio.connected, audio.airplay_active);
    if (audio.generation) {
      sony_display_mode_t mode = sony_screen_display_mode();
      int64_t now = esp_timer_get_time();
      if (now >= next_text || mode != last_mode || audio.generation != last_generation) {
        sony_radio_status_t indicators = sony_radio_status(
            audio.fresh && audio.playing, audio.channels,
            audio.metadata.bitrate, audio.metadata.genre);
        indicators.searching = audio.fresh && !audio.playing && !audio.failed;
        indicators.generation = audio.generation;
        indicators.preset = audio.acknowledged ? audio.preset : 0;
        if (indicators.playing && !audio.airplay)
          indicators.audio |= audio.genre_labels;
        char text[768];
        if (!audio.fresh) {
          snprintf(text, sizeof(text), "%s", audio.acknowledged ?
                   "AUDIO OFFLINE" : "AUDIO UNCONFIRMED");
        } else {
          sony_display_metadata_t metadata = {
              .title = audio.title, .station = audio.station,
              .source = audio.airplay ? "AirPlay" : "Radio",
              .codec = audio.metadata.codec, .bitrate = audio.metadata.bitrate,
              .genre = audio.metadata.genre, .url = audio.metadata.url,
              .ip = audio.metadata.ip,
              .sample_rate = audio.sample_rate, .channels = audio.channels,
              .receiving = audio.playing, .failed = audio.failed};
          sony_display_mode_text(mode, text, sizeof(text), &metadata);
        }
        sony_vfd_show_radio_mode(text, &indicators, mode);
        last_mode = mode;
        last_generation = audio.generation;
        next_text = now + 200000;
      }
      if (mode == SONY_DISPLAY_EQUALIZER)
        sony_vfd_show_spectrum(audio.levels);
      else if (mode == SONY_DISPLAY_VU_STEREO || mode == SONY_DISPLAY_VU_CENTER)
        sony_vfd_show_vu(audio.rms, audio.peak, audio.held);
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(settings_init());
  ESP_ERROR_CHECK(sony_uart_link_init());
  ESP_ERROR_CHECK(sony_screen_init());
  ESP_ERROR_CHECK(radio_init());
  ESP_ERROR_CHECK(sony_controller_menu_init());
#ifdef CONFIG_SONY_STARTUP_ISOLATED_DISPLAY
  if (sony_screen_startup_boot_isolated() == ESP_OK) {
    const int64_t deadline = esp_timer_get_time() + 7200000;
    while (sony_screen_is_running() && esp_timer_get_time() < deadline)
      vTaskDelay(pdMS_TO_TICKS(20));
    if (sony_screen_is_running()) sony_screen_stop();
  }
#endif
  esp_err_t display_err = sony_screen_display_button_press(false);
  if (display_err != ESP_OK)
    ESP_LOGE("controller", "Display autostart failed: %s", esp_err_to_name(display_err));
  ESP_ERROR_CHECK(sony_keypad_init());
  ESP_ERROR_CHECK(xTaskCreatePinnedToCore(audio_display_task, "audio_display",
                                          5120, NULL, 2, NULL, 0) == pdPASS
                      ? ESP_OK : ESP_ERR_NO_MEM);
  esp_vfs_spiffs_conf_t storage = {
      .base_path = "/spiffs", .partition_label = "storage",
      .max_files = 5, .format_if_mount_failed = false,
  };
  ESP_ERROR_CHECK(esp_vfs_spiffs_register(&storage));
  wifi_init_apsta(NULL, NULL);
  start_http();
}
