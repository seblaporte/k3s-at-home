#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include "x10a_protocol.h"

#include <vector>

namespace esphome::daikin_x10a {

// Lit les registres d'une Daikin Altherma sur le port X10A (protocole « I », 9600 8E1).
// Contrairement à ESPAltherma, aucune attente bloquante : un registre est interrogé à la fois
// depuis loop(), pour ne pas gêner le Wi-Fi, l'API et le BLE qui tournent sur le même cœur.
class DaikinX10A : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

#ifdef USE_SENSOR
  void add_sensor(sensor::Sensor *s, uint8_t reg, uint8_t offset, uint8_t size, uint16_t convid);
#endif
#ifdef USE_TEXT_SENSOR
  void add_text_sensor(text_sensor::TextSensor *s, uint8_t reg, uint8_t offset, uint8_t size, uint16_t convid);
#endif
#ifdef USE_BINARY_SENSOR
  void add_binary_sensor(binary_sensor::BinarySensor *s, uint8_t reg, uint8_t offset, uint8_t size,
                         uint16_t convid);
#endif

 protected:
  enum class State { IDLE, SEND, WAIT, PAUSE };

  struct Value {
    uint8_t reg;
    uint8_t offset;
    uint8_t size;
    uint16_t convid;
    EntityBase *entity;
  };

  void add_value_(EntityBase *entity, uint8_t reg, uint8_t offset, uint8_t size, uint16_t convid);
  void send_query_();
  void read_reply_();
  void on_reply_();
  void on_failure_(const char *reason);
  void next_register_();
  void publish_(const Value &v, const uint8_t *data);

  std::vector<Value> values_;
  std::vector<uint8_t> registers_;

  State state_{State::IDLE};
  size_t index_{0};
  uint8_t tries_{0};
  uint8_t errors_{0};
  uint32_t started_ms_{0};
  uint8_t buf_[64];
  size_t len_{0};
  size_t expected_{0};
};

}  // namespace esphome::daikin_x10a
