#include "serial_rpc_component.h"
#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/logger/logger.h"
#include "esphome/components/network/util.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace serial_rpc {

static const char *const TAG = "serial_rpc";
const char *const SerialRpcComponent::MAGIC_HEADER = "JRPC:";

void SerialRpcComponent::setup() {
  global_serial_rpc_component = this;
#ifdef USE_ARDUINO
  this->hw_serial_ = logger::global_logger->get_hw_serial();
#else
  ESP_LOGE(TAG, "Serial RPC component is only supported with Arduino.");
#endif

#ifdef USE_WIFI
  if (!wifi::global_wifi_component->has_sta()) {
    wifi::global_wifi_component->start_scanning();
  }
#endif

  ESP_LOGCONFIG(TAG, "Serial RPC initialized");
}

void SerialRpcComponent::dump_config() { 
  ESP_LOGCONFIG(TAG, "Serial RPC:"); 
}

void SerialRpcComponent::loop() {
#ifdef USE_ARDUINO
  while (this->hw_serial_->available()) {
    char c = this->hw_serial_->read();
    
    if (!this->reading_line_) {
      if (c == '\r' || c == '\n')
        continue;
      
      this->reading_line_ = true;
      this->buffer_ = c;
      
      if (c == MAGIC_HEADER[0]) {
        this->reading_json_rpc_ = true;
      } else {
        this->reading_json_rpc_ = false;
      }
    } else {
      if (c == '\r' || c == '\n') {
        this->reading_line_ = false;
        
        if (this->reading_json_rpc_ && this->buffer_.size() >= strlen(MAGIC_HEADER)) {
          if (this->buffer_.substr(0, strlen(MAGIC_HEADER)) == MAGIC_HEADER) {
            std::string json_data = this->buffer_.substr(strlen(MAGIC_HEADER));
            this->process_line_(json_data);
          }
        }
        
        this->buffer_.clear();
        this->reading_json_rpc_ = false;
      } else {
        this->buffer_ += c;
      }
    }
  }
  
#ifdef USE_WIFI
  if (!this->connecting_sta_.get_ssid().empty() && wifi::global_wifi_component->is_connected()) {
    std::string ssid = this->connecting_sta_.get_ssid();
    
    wifi::global_wifi_component->save_wifi_sta(ssid, this->connecting_sta_.get_password());
    this->connecting_sta_ = {};
    this->cancel_timeout("wifi-connect-timeout");
    
    auto event_builder = [ssid](JsonObject root) {
      root["jsonrpc"] = "2.0";
      root["method"] = "wifi.connect.success";
      root["params"]["ssid"] = ssid;
    };
    
    std::string event = json::build_json(event_builder);
    this->send_response_(event);
    
    ESP_LOGI(TAG, "Successfully connected to WiFi network '%s'", ssid.c_str());
  }
#endif
#endif
}

void SerialRpcComponent::process_line_(const std::string &line) {
  DynamicJsonDocument request_doc(2048);
  DeserializationError error = deserializeJson(request_doc, line);
  
  if (error) {
    ESP_LOGW(TAG, "Failed to parse JSON-RPC request: %s", error.c_str());
    auto error_builder = [](JsonObject root) {
      root["jsonrpc"] = "2.0";
      root["error"]["code"] = -32700;
      root["error"]["message"] = "Parse error";
      root["id"] = nullptr;
    };
    
    std::string error_response = json::build_json(error_builder);
    this->send_response_(error_response);
    return;
  }
  
  JsonObject request = request_doc.as<JsonObject>();
  
  if (!request.containsKey("jsonrpc") || !request.containsKey("method") || !request.containsKey("id")) {
    ESP_LOGW(TAG, "Invalid JSON-RPC request: missing required fields");
    auto error_builder = [&request](JsonObject root) {
      root["jsonrpc"] = "2.0";
      root["error"]["code"] = -32600;
      root["error"]["message"] = "Invalid Request";
      root["id"] = request.containsKey("id") ? request["id"] : nullptr;
    };
    
    std::string error_response = json::build_json(error_builder);
    this->send_response_(error_response);
    return;
  }

  auto response = json::build_json([this, &request](JsonObject response_obj) {
    response_obj["jsonrpc"] = "2.0";
    response_obj["id"] = request["id"];
    
    std::string method = request["method"];
    
    if (method == "device.info") {
      this->handle_device_info_(request, response_obj);
    } else if (method == "entity.get") {
      this->handle_get_entity_(request, response_obj);
    } else if (method == "entity.set") {
      this->handle_set_entity_(request, response_obj);
    } else if (method == "button.press") {
      this->handle_button_press_(request, response_obj);
    } else if (method == "wifi.settings") {
      this->handle_wifi_settings_(request, response_obj);
    } else if (method == "wifi.scan") {
      this->handle_get_wifi_networks_(request, response_obj);
    } else {
      ESP_LOGW(TAG, "Unknown method: %s", method.c_str());
      response_obj["error"]["code"] = -32601;
      response_obj["error"]["message"] = "Method not found";
    }
  });

  this->send_response_(response);
}

void SerialRpcComponent::handle_device_info_(JsonObject &request, JsonObject &response) {
  JsonObject result = response.createNestedObject("result");
  
  result["name"] = App.get_name();
  
  std::string ip_address;
  for (auto &ip : network::get_ip_addresses()) {
    if (ip.is_ip4()) {
      ip_address = ip.str();
      break;
    }
  }
  result["ip_address"] = ip_address;
  
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->has_sta()) {
    result["ssid"] = wifi::global_wifi_component->wifi_ssid();
  } else {
    result["ssid"] = "";
  }
#else
  result["ssid"] = "";
#endif

  result["esphome_version"] = ESPHOME_VERSION;
#ifdef ESPHOME_PROJECT_VERSION
  result["project_version"] = ESPHOME_PROJECT_VERSION;
#else
  result["project_version"] = "";
#endif
}

void SerialRpcComponent::handle_get_entity_(JsonObject &request, JsonObject &response) {
  if (!request.containsKey("params") || !request["params"].is<JsonObject>() ||
      !request["params"].as<JsonObject>().containsKey("id") || 
      !request["params"].as<JsonObject>().containsKey("type")) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Invalid params";
    return;
  }
  
  JsonObject params = request["params"];
  std::string entity_id = params["id"].as<std::string>();
  uint8_t entity_type = params["type"].as<uint8_t>();
  
  JsonObject result = response.createNestedObject("result");
  result["id"] = entity_id;
  result["type"] = entity_type;
  
  bool found = false;
  
  switch (entity_type) {
#ifdef USE_TEXT
    case ENTITY_TYPE_TEXT: {
      for (auto *obj : App.get_texts()) {
        if (obj->get_object_id() == entity_id) {
          result["value"] = obj->state;
          result["mode"] = static_cast<int>(obj->traits.get_mode());
          result["min_length"] = obj->traits.get_min_length();
          result["max_length"] = obj->traits.get_max_length();
          result["pattern"] = obj->traits.get_pattern();
          found = true;
          break;
        }
      }
      break;
    }
#endif
      
#ifdef USE_SELECT
    case ENTITY_TYPE_SELECT: {
      for (auto *obj : App.get_selects()) {
        if (obj->get_object_id() == entity_id) {
          result["value"] = obj->state;
          JsonArray options = result.createNestedArray("options");
          for (auto &option : obj->traits.get_options()) {
            options.add(option);
          }
          found = true;
          break;
        }
      }
      break;
    }
#endif
      
#ifdef USE_SWITCH
    case ENTITY_TYPE_SWITCH: {
      for (auto *obj : App.get_switches()) {
        if (obj->get_object_id() == entity_id) {
          result["value"] = obj->state ? "ON" : "OFF";
          found = true;
          break;
        }
      }
      break;
    }
#endif
      
    default:
      response["error"]["code"] = -32602;
      response["error"]["message"] = "Unsupported entity type";
      return;
  }
  
  if (!found) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Entity not found";
    response.remove("result");
  }
}

void SerialRpcComponent::handle_set_entity_(JsonObject &request, JsonObject &response) {
  if (!request.containsKey("params") || !request["params"].is<JsonObject>() ||
      !request["params"].as<JsonObject>().containsKey("id") || 
      !request["params"].as<JsonObject>().containsKey("type") ||
      !request["params"].as<JsonObject>().containsKey("value")) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Invalid params";
    return;
  }
  
  JsonObject params = request["params"];
  std::string entity_id = params["id"].as<std::string>();
  uint8_t entity_type = params["type"].as<uint8_t>();
  std::string value = params["value"].as<std::string>();
  
  JsonObject result = response.createNestedObject("result");
  result["id"] = entity_id;
  result["type"] = entity_type;
  
  bool found = false;
  bool success = false;
  
  switch (entity_type) {
#ifdef USE_TEXT
    case ENTITY_TYPE_TEXT: {
      for (auto *obj : App.get_texts()) {
        if (obj->get_object_id() == entity_id) {
          found = true;
          auto call = obj->make_call();
          call.set_value(value);
          call.perform();
          success = true;
          break;
        }
      }
      break;
    }
#endif
      
#ifdef USE_SELECT
    case ENTITY_TYPE_SELECT: {
      for (auto *obj : App.get_selects()) {
        if (obj->get_object_id() == entity_id) {
          found = true;
          auto call = obj->make_call();
          call.set_option(value);
          call.perform();
          success = true;
          break;
        }
      }
      break;
    }
#endif
      
#ifdef USE_SWITCH
    case ENTITY_TYPE_SWITCH: {
      for (auto *obj : App.get_switches()) {
        if (obj->get_object_id() == entity_id) {
          found = true;
          if (value == "ON") {
            obj->turn_on();
            success = true;
          } else if (value == "OFF") {
            obj->turn_off();
            success = true;
          } else {
            response["error"]["code"] = -32602;
            response["error"]["message"] = "Invalid value for switch (must be 'ON' or 'OFF')";
            response.remove("result");
            return;
          }
          break;
        }
      }
      break;
    }
#endif
      
    default:
      response["error"]["code"] = -32602;
      response["error"]["message"] = "Unsupported entity type";
      response.remove("result");
      return;
  }
  
  if (!found) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Entity not found";
    response.remove("result");
    return;
  }
  
  if (success) {
    result["success"] = true;
  } else {
    response["error"]["code"] = -32603;
    response["error"]["message"] = "Internal error";
    response.remove("result");
  }
}

void SerialRpcComponent::handle_button_press_(JsonObject &request, JsonObject &response) {
  if (!request.containsKey("params") || !request["params"].is<JsonObject>() ||
      !request["params"].as<JsonObject>().containsKey("id")) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Invalid params";
    return;
  }
  
  JsonObject params = request["params"];
  std::string button_id = params["id"].as<std::string>();
  
  JsonObject result = response.createNestedObject("result");
  result["id"] = button_id;
  
  bool found = false;
  bool success = false;
  
#ifdef USE_BUTTON
  for (auto *obj : App.get_buttons()) {
    if (obj->get_object_id() == button_id) {
      found = true;
      obj->press();
      success = true;
      break;
    }
  }
#endif
  
  if (!found) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Button not found";
    response.remove("result");
    return;
  }
  
  if (success) {
    result["success"] = true;
  } else {
    response["error"]["code"] = -32603;
    response["error"]["message"] = "Internal error";
    response.remove("result");
  }
}

void SerialRpcComponent::handle_wifi_settings_(JsonObject &request, JsonObject &response) {
#ifdef USE_WIFI
  if (!request.containsKey("params") || !request["params"].is<JsonObject>() ||
      !request["params"].as<JsonObject>().containsKey("ssid") || 
      !request["params"].as<JsonObject>().containsKey("password")) {
    response["error"]["code"] = -32602;
    response["error"]["message"] = "Invalid params";
    return;
  }
  
  JsonObject params = request["params"];
  std::string ssid = params["ssid"].as<std::string>();
  std::string password = params["password"].as<std::string>();
  
  wifi::WiFiAP sta{};
  sta.set_ssid(ssid);
  sta.set_password(password);
  this->connecting_sta_ = sta;
  
  wifi::global_wifi_component->set_sta(sta);
  wifi::global_wifi_component->start_connecting(sta, false);
  
  ESP_LOGD(TAG, "Connecting to WiFi network ssid=%s, password=" LOG_SECRET("%s"), ssid.c_str(), password.c_str());
  
  auto f = std::bind(&SerialRpcComponent::on_wifi_connect_timeout_, this);
  this->set_timeout("wifi-connect-timeout", 30000, f);
  
  JsonObject result = response.createNestedObject("result");
  result["connecting"] = true;
  result["ssid"] = ssid;
#else
  response["error"]["code"] = -32601;
  response["error"]["message"] = "WiFi not supported";
#endif
}

void SerialRpcComponent::handle_get_wifi_networks_(JsonObject &request, JsonObject &response) {
#ifdef USE_WIFI
  wifi::global_wifi_component->start_scanning();
  
  JsonObject result = response.createNestedObject("result");
  JsonArray networks = result.createNestedArray("networks");
  
  auto scan_results = wifi::global_wifi_component->get_scan_result();
  std::vector<std::string> added_ssids;
  
  for (auto &scan : scan_results) {
    if (scan.get_is_hidden())
      continue;
      
    const std::string &ssid = scan.get_ssid();
    if (std::find(added_ssids.begin(), added_ssids.end(), ssid) != added_ssids.end())
      continue;
    
    JsonObject network = networks.createNestedObject();
    network["ssid"] = ssid;
    network["rssi"] = scan.get_rssi();
    network["channel"] = scan.get_channel();
    network["auth"] = scan.get_with_auth();
    
    added_ssids.push_back(ssid);
  }
#else
  response["error"]["code"] = -32601;
  response["error"]["message"] = "WiFi not supported";
#endif
}

void SerialRpcComponent::on_wifi_connect_timeout_() {
#ifdef USE_WIFI
  ESP_LOGW(TAG, "Timed out trying to connect to WiFi network");
  wifi::global_wifi_component->clear_sta();
  
  auto event_builder = [](JsonObject root) {
    root["jsonrpc"] = "2.0";
    root["method"] = "wifi.connect.error";
    root["params"]["message"] = "Failed to connect to WiFi network";
  };
  
  std::string event = json::build_json(event_builder);
  this->send_response_(event);
#endif
}

void SerialRpcComponent::send_response_(const std::string &json_response) {
  std::string full_response = MAGIC_HEADER + json_response + "\r\n";
  
#ifdef USE_ARDUINO
  size_t total_bytes_written = 0;

  while (total_bytes_written < full_response.length()) {
    size_t bytes_written = this->hw_serial_->write(full_response.c_str() + total_bytes_written, full_response.length() - total_bytes_written);

    if (bytes_written < 0) {
      ESP_LOGE(TAG, "Failed to write to serial port");
      return;
    }

    total_bytes_written += bytes_written;
  }
#endif
}

SerialRpcComponent *global_serial_rpc_component = nullptr;

}  // namespace serial_rpc
}  // namespace esphome
