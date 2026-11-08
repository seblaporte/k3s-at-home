#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#include "inmotion_protocol.h"

#include <string>
#include <vector>

#ifdef USE_ESP32
#include <esp_gattc_api.h>

namespace esphome::inmotion_v1 {

namespace espbt = esphome::esp32_ble_tracker;

class InMotionV1 : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_password(const std::string &password) { this->password_ = password; }
  void set_better_percents(bool better) { this->better_percents_ = better; }
  void set_publish_interval(uint32_t ms) { this->publish_interval_ = ms; }
  void set_voltage_sensor(sensor::Sensor *s) { this->voltage_sensor_ = s; }
  void set_battery_level_sensor(sensor::Sensor *s) { this->battery_level_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { this->current_sensor_ = s; }
  void set_temperature_sensor(sensor::Sensor *s) { this->temperature_sensor_ = s; }
  void set_charging_sensor(binary_sensor::BinarySensor *s) { this->charging_sensor_ = s; }
  void set_online_sensor(binary_sensor::BinarySensor *s) { this->online_sensor_ = s; }

 protected:
  enum class Phase { IDLE, PASSWORD, SLOW_INFO, FAST_INFO };

  void on_connected_();
  void on_disconnected_();
  void handle_message_(const inmotion::Message &m);
  void handle_fast_(const inmotion::FastInfo &f);
  void queue_request_();
  void send_pending_chunk_();
  void publish_averages_();
  void set_online_(bool online);

  // Configuration
  std::string password_{"000000"};
  bool better_percents_{false};
  uint32_t publish_interval_{2000};

  // BLE : handles bruts, car ESPHome libère le cache GATT une fois la connexion établie
  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};

  // Protocole
  Phase phase_{Phase::IDLE};
  int password_tries_{0};
  inmotion::Unpacker unpacker_;
  inmotion::Message msg_;
  inmotion::VoltageFilter voltage_filter_;
  std::vector<uint8_t> tx_;
  size_t tx_pos_{0};
  uint32_t last_chunk_ms_{0};
  uint32_t last_request_ms_{0};
  uint32_t last_fast_ms_{0};

  // Moyennes entre deux publications
  float voltage_sum_{0}, current_sum_{0};
  int voltage_count_{0}, current_count_{0};
  int8_t last_temperature_{0};
  bool have_temperature_{false};
  uint32_t last_publish_ms_{0};
  uint32_t rejected_voltage_{0};
  bool online_{false};

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  binary_sensor::BinarySensor *charging_sensor_{nullptr};
  binary_sensor::BinarySensor *online_sensor_{nullptr};
};

}  // namespace esphome::inmotion_v1

#endif  // USE_ESP32
