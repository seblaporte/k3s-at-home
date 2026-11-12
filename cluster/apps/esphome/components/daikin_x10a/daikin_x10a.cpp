#include "daikin_x10a.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::daikin_x10a {

static const char *const TAG = "daikin_x10a";

static const uint32_t REPLY_TIMEOUT_MS = 300;  // ESPAltherma : SER_TIMEOUT
static const uint32_t RETRY_DELAY_MS = 1000;
static const uint8_t MAX_TRIES = 3;
static const size_t DEFAULT_REPLY_LEN = 12;  // remplacée par la longueur annoncée au 3e octet

void DaikinX10A::add_value_(EntityBase *entity, uint8_t reg, uint8_t offset, uint8_t size, uint16_t convid) {
  this->values_.push_back({reg, offset, size, convid, entity});
  if (std::find(this->registers_.begin(), this->registers_.end(), reg) == this->registers_.end())
    this->registers_.push_back(reg);
}

#ifdef USE_SENSOR
void DaikinX10A::add_sensor(sensor::Sensor *s, uint8_t reg, uint8_t offset, uint8_t size, uint16_t convid) {
  this->add_value_(s, reg, offset, size, convid);
}
#endif
#ifdef USE_TEXT_SENSOR
void DaikinX10A::add_text_sensor(text_sensor::TextSensor *s, uint8_t reg, uint8_t offset, uint8_t size,
                                 uint16_t convid) {
  this->add_value_(s, reg, offset, size, convid);
}
#endif
#ifdef USE_BINARY_SENSOR
void DaikinX10A::add_binary_sensor(binary_sensor::BinarySensor *s, uint8_t reg, uint8_t offset, uint8_t size,
                                   uint16_t convid) {
  this->add_value_(s, reg, offset, size, convid);
}
#endif

void DaikinX10A::setup() {}

void DaikinX10A::dump_config() {
  ESP_LOGCONFIG(TAG, "Daikin Altherma X10A");
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  %u valeurs sur %u registres", (unsigned) this->values_.size(),
                (unsigned) this->registers_.size());
  for (auto reg : this->registers_)
    ESP_LOGCONFIG(TAG, "    registre 0x%02X", reg);
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
}

void DaikinX10A::update() {
  if (this->registers_.empty())
    return;
  if (this->state_ != State::IDLE) {
    ESP_LOGW(TAG, "Cycle précédent pas terminé, interrogation sautée (update_interval trop court ?)");
    return;
  }
  this->index_ = 0;
  this->tries_ = 0;
  this->errors_ = 0;
  this->state_ = State::SEND;
}

void DaikinX10A::loop() {
  switch (this->state_) {
    case State::IDLE:
      // Octets parasites hors cycle : on les jette
      while (this->available())
        this->read();
      break;
    case State::PAUSE:
      if (millis() - this->started_ms_ >= RETRY_DELAY_MS)
        this->state_ = State::SEND;
      break;
    case State::SEND:
      this->send_query_();
      break;
    case State::WAIT:
      this->read_reply_();
      break;
  }
}

void DaikinX10A::send_query_() {
  while (this->available())
    this->read();
  uint8_t query[4];
  x10a::build_query(this->registers_[this->index_], query);
  ESP_LOGV(TAG, "Interrogation du registre 0x%02X", query[2]);
  this->write_array(query, sizeof(query));
  this->len_ = 0;
  this->expected_ = DEFAULT_REPLY_LEN;
  this->started_ms_ = millis();
  this->state_ = State::WAIT;
}

void DaikinX10A::read_reply_() {
  // available() d'abord : read_byte() sur un tampon vide attend et journalise une erreur
  while (this->len_ < this->expected_ && this->available()) {
    this->buf_[this->len_++] = this->read();
    if (this->len_ == 2 && this->buf_[0] == x10a::ERROR_REPLY_0 && this->buf_[1] == x10a::ERROR_REPLY_1) {
      this->on_failure_("la PAC ne comprend pas la requête (0x15 0xEA), protocole S ?");
      return;
    }
    if (this->len_ == x10a::HEADER_LEN)
      this->expected_ = std::min<size_t>(this->buf_[2] + 2, sizeof(this->buf_));
  }
  if (this->len_ >= this->expected_) {
    this->on_reply_();
    return;
  }
  if (millis() - this->started_ms_ > REPLY_TIMEOUT_MS)
    this->on_failure_(this->len_ == 0 ? "aucune réponse, vérifier le câblage" : "réponse incomplète");
}

void DaikinX10A::on_reply_() {
  uint8_t reg = this->registers_[this->index_];
  ESP_LOGVV(TAG, "Réponse 0x%02X : %s", reg, format_hex_pretty(this->buf_, this->len_).c_str());
  if (x10a::crc(this->buf_, this->len_ - 1) != this->buf_[this->len_ - 1]) {
    this->on_failure_("CRC invalide");
    return;
  }
  if (this->buf_[1] != reg) {
    this->on_failure_("réponse pour un autre registre");
    return;
  }
  for (const auto &v : this->values_) {
    if (v.reg != reg)
      continue;
    size_t start = x10a::HEADER_LEN + v.offset;
    if (start + v.size > this->len_ - 1) {
      ESP_LOGW(TAG, "Registre 0x%02X : offset %u hors de la réponse (%u octets)", reg, v.offset,
               (unsigned) this->len_);
      continue;
    }
    this->publish_(v, this->buf_ + start);
  }
  this->next_register_();
}

void DaikinX10A::publish_(const Value &v, const uint8_t *data) {
  switch (x10a::kind_of(v.convid)) {
#ifdef USE_SENSOR
    case x10a::Kind::NUMBER:
      static_cast<sensor::Sensor *>(v.entity)->publish_state(x10a::to_number(v.convid, data, v.size));
      break;
#endif
#ifdef USE_TEXT_SENSOR
    case x10a::Kind::TEXT:
      static_cast<text_sensor::TextSensor *>(v.entity)->publish_state(x10a::to_text(v.convid, data, v.size));
      break;
#endif
#ifdef USE_BINARY_SENSOR
    case x10a::Kind::BINARY:
      static_cast<binary_sensor::BinarySensor *>(v.entity)->publish_state(x10a::to_binary(v.convid, data));
      break;
#endif
    default:
      break;
  }
}

void DaikinX10A::on_failure_(const char *reason) {
  uint8_t reg = this->registers_[this->index_];
  if (++this->tries_ < MAX_TRIES) {
    ESP_LOGD(TAG, "Registre 0x%02X : %s, nouvel essai", reg, reason);
    this->started_ms_ = millis();
    this->state_ = State::PAUSE;
    return;
  }
  ESP_LOGW(TAG, "Registre 0x%02X : %s, abandon après %u essais", reg, reason, MAX_TRIES);
  this->errors_++;
  this->next_register_();
}

void DaikinX10A::next_register_() {
  this->tries_ = 0;
  if (++this->index_ < this->registers_.size()) {
    this->state_ = State::SEND;
    return;
  }
  this->state_ = State::IDLE;
  if (this->errors_ == 0) {
    this->status_clear_warning();
  } else {
    this->status_set_warning();
  }
}

}  // namespace esphome::daikin_x10a
