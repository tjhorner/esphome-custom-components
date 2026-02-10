#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"
#include "esphome/core/version.h"

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif

#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif

#include <vector>
#include <ArduinoJson.h>

#ifdef USE_ESP32
#include <driver/uart.h>
#if defined(USE_ESP32_VARIANT_ESP32C3) || defined(USE_ESP32_VARIANT_ESP32C6) || defined(USE_ESP32_VARIANT_ESP32C61) || \
    defined(USE_ESP32_VARIANT_ESP32H2) || defined(USE_ESP32_VARIANT_ESP32S3)
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#endif
#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3)
#include <esp_private/usb_console.h>
#endif
#elif defined(USE_ARDUINO)
#include <HardwareSerial.h>
#endif

namespace esphome {
namespace serial_rpc {

enum EntityType : uint8_t {
  ENTITY_TYPE_TEXT = 0x01,
  ENTITY_TYPE_SELECT = 0x02,
  ENTITY_TYPE_SWITCH = 0x03,
  ENTITY_TYPE_BUTTON = 0x04,
};

class SerialRpcComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  void process_line_(const std::string &line);
  void handle_device_info_(JsonObject &request, JsonObject &response);
  void handle_get_entity_(JsonObject &request, JsonObject &response);
  void handle_set_entity_(JsonObject &request, JsonObject &response);
  void handle_button_press_(JsonObject &request, JsonObject &response);
  void handle_wifi_settings_(JsonObject &request, JsonObject &response);
  void handle_get_wifi_networks_(JsonObject &request, JsonObject &response);
  void on_wifi_connect_timeout_();
  void send_response_(const std::string &response);

  optional<uint8_t> read_byte_();
  void write_data_(const uint8_t *data, size_t size);
  
  std::string buffer_;
  bool reading_line_{false};
  bool reading_json_rpc_{false};
  
#ifdef USE_WIFI
  wifi::WiFiAP connecting_sta_{};
#endif
  
  static const char *const MAGIC_HEADER;

#ifdef USE_ESP32
  uart_port_t uart_num_;
#elif defined(USE_ARDUINO)
  Stream *hw_serial_{nullptr};
#endif
};

extern SerialRpcComponent *global_serial_rpc_component;

}  // namespace serial_rpc
}  // namespace esphome
