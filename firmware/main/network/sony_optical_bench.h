#pragma once

#include "esp_http_server.h"

esp_err_t sony_optical_bench_init(void);
esp_err_t sony_optical_bench_run(httpd_req_t *req);
