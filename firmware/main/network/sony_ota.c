#include "sony_ota.h"
#include "sdkconfig.h"

#ifdef SONY_OTA_HAS_KEY
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "sony_ota_public.h"
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_SONY_CONTROLLER_NODE
static const char role[] = "DAR-1000ES OTA v1:controller";
#else
static const char role[] = "DAR-1000ES OTA v1:audio";
#endif

static bool receive(httpd_req_t *req, void *buffer, size_t size) {
  char *out = buffer;
  while (size) {
    size_t chunk = size > 4096 ? 4096 : size;
    int n = httpd_req_recv(req, out, chunk);
    if (n <= 0) return false;
    out += n;
    size -= n;
  }
  return true;
}

static bool signature_valid(const unsigned char hash[32],
                            const unsigned char *signature, size_t size) {
  unsigned char message[sizeof(role) + 32], digest[32];
  memcpy(message, role, sizeof(role));
  memcpy(message + sizeof(role), hash, 32);
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  bool valid = mbedtls_sha256(message, sizeof(message), digest, 0) == 0 &&
      mbedtls_pk_parse_public_key(&key, ota_key, sizeof(ota_key)) == 0 &&
      mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) &&
      mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, size) == 0;
  mbedtls_pk_free(&key);
  return valid;
}
#endif

esp_err_t sony_ota_update(httpd_req_t *req) {
#ifndef SONY_OTA_HAS_KEY
  httpd_resp_set_status(req, "503 Service Unavailable");
  return httpd_resp_sendstr(req, "No OTA signing key configured.");
#else
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
  if (!running || !boot || !target || boot->address != running->address) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_sendstr(req, "No inactive slot available, or update already staged.");
  }
  unsigned char header[37], signature[72];
  if (req->content_len <= sizeof(header) ||
      req->content_len > target->size + sizeof(header) + sizeof(signature) ||
      !receive(req, header, sizeof(header)) || memcmp(header, "SOTA", 4) ||
      !header[36] || header[36] > sizeof(signature) ||
      req->content_len <= sizeof(header) + header[36])
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OTA bundle.");
  size_t size = req->content_len - sizeof(header) - header[36];
  if (size > target->size || !receive(req, signature, header[36]))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OTA bundle size.");
  if (!signature_valid(header + 4, signature, header[36])) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_sendstr(req, "Signature rejected for this ESP role.");
  }

  unsigned char *image = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!image)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough PSRAM.");
  unsigned char digest[32];
  if (!receive(req, image, size) || mbedtls_sha256(image, size, digest, 0) != 0 ||
      memcmp(digest, header + 4, sizeof(digest))) {
    free(image);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware digest mismatch.");
  }

  esp_ota_handle_t handle;
  esp_err_t err = esp_ota_begin(target, size, &handle);
  if (err == ESP_OK) {
    err = esp_ota_write(handle, image, size);
    if (err == ESP_OK) err = esp_ota_end(handle);
    else esp_ota_abort(handle);
  }
  free(image);
  if (err == ESP_OK) err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
  return httpd_resp_sendstr(req, "Verified and staged. No reboot requested.\n");
#endif
}
