#include "inmotion_v1.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>

#ifdef USE_ESP32

namespace esphome::inmotion_v1 {

static const char *const TAG = "inmotion_v1";

static const uint16_t NOTIFY_SERVICE = 0xFFE0;
static const uint16_t NOTIFY_CHAR = 0xFFE4;
static const uint16_t WRITE_SERVICE = 0xFFE5;
static const uint16_t WRITE_CHAR = 0xFFE9;

static const uint32_t REQUEST_PERIOD_MS = 250;  // WheelLog : une requête tous les 10 × 25 ms
static const size_t CHUNK_SIZE = 20;
static const uint32_t CHUNK_DELAY_MS = 20;
static const int PASSWORD_MAX_TRIES = 6;
static const uint32_t ONLINE_TIMEOUT_MS = 5000;
static const int MODEL_V10F = 141;

void InMotionV1::setup() {
  if (this->online_sensor_ != nullptr)
    this->online_sensor_->publish_initial_state(false);
  if (this->charging_sensor_ != nullptr)
    this->charging_sensor_->publish_initial_state(false);
}

void InMotionV1::dump_config() {
  ESP_LOGCONFIG(TAG, "InMotion V1 (V10F)");
  ESP_LOGCONFIG(TAG, "  Pourcentage : formule %s", this->better_percents_ ? "« better »" : "standard");
  ESP_LOGCONFIG(TAG, "  Publication : toutes les %u ms", (unsigned) this->publish_interval_);
  LOG_SENSOR("  ", "Tension", this->voltage_sensor_);
  LOG_SENSOR("  ", "Batterie", this->battery_level_sensor_);
  LOG_SENSOR("  ", "Courant", this->current_sensor_);
  LOG_SENSOR("  ", "Température", this->temperature_sensor_);
  LOG_BINARY_SENSOR("  ", "En charge", this->charging_sensor_);
  LOG_BINARY_SENSOR("  ", "En ligne", this->online_sensor_);
}

void InMotionV1::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                     esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *notify = this->parent()->get_characteristic(espbt::ESPBTUUID::from_uint16(NOTIFY_SERVICE),
                                                        espbt::ESPBTUUID::from_uint16(NOTIFY_CHAR));
      auto *write = this->parent()->get_characteristic(espbt::ESPBTUUID::from_uint16(WRITE_SERVICE),
                                                       espbt::ESPBTUUID::from_uint16(WRITE_CHAR));
      if (notify == nullptr || write == nullptr) {
        ESP_LOGE(TAG, "Caractéristiques FFE4/FFE9 introuvables : est-ce bien une roue InMotion V1 ?");
        this->status_set_error();
        break;
      }
      this->notify_handle_ = notify->handle;
      this->write_handle_ = write->handle;
      // Appel ESP-IDF direct : BLEClientBase::register_for_notify() n'existe qu'à partir
      // d'ESPHome 2026.x récents. Sans risque ici : on ne passe à ESTABLISHED (ce qui
      // autorise la libération du cache GATT) qu'à la réception de REG_FOR_NOTIFY_EVT.
      auto err = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(),
                                                   this->notify_handle_);
      if (err != ESP_OK)
        ESP_LOGW(TAG, "register_for_notify a échoué : %d", err);
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Activation des notifications FFE4 refusée : %d", param->reg_for_notify.status);
        break;
      }
      // Le CCCD 0x2902 est écrit par BLEClientBase juste avant ce point
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->on_connected_();
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->notify_handle_)
        break;
      ESP_LOGV(TAG, "RX %s", format_hex_pretty(param->notify.value, param->notify.value_len).c_str());
      for (uint16_t i = 0; i < param->notify.value_len; i++) {
        if (this->unpacker_.feed(param->notify.value[i], this->msg_))
          this->handle_message_(this->msg_);
      }
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->on_disconnected_();
      break;
    default:
      break;
  }
}

void InMotionV1::on_connected_() {
  ESP_LOGI(TAG, "Connecté, notifications actives ; envoi du mot de passe");
  this->status_clear_error();
  this->phase_ = Phase::PASSWORD;
  this->password_tries_ = 0;
  this->tx_.clear();
  this->tx_pos_ = 0;
  this->voltage_filter_.reset();
  this->last_fast_ms_ = millis();
}

void InMotionV1::on_disconnected_() {
  if (this->phase_ != Phase::IDLE)
    ESP_LOGW(TAG, "Roue déconnectée (éteinte ou hors de portée)");
  this->phase_ = Phase::IDLE;
  this->notify_handle_ = 0;
  this->write_handle_ = 0;
  this->tx_.clear();
  this->tx_pos_ = 0;
  this->set_online_(false);
}

void InMotionV1::loop() {
  if (this->phase_ == Phase::IDLE || this->node_state != espbt::ClientState::ESTABLISHED)
    return;
  uint32_t now = millis();

  if (this->tx_pos_ < this->tx_.size()) {
    if (now - this->last_chunk_ms_ >= CHUNK_DELAY_MS)
      this->send_pending_chunk_();
  } else if (now - this->last_request_ms_ >= REQUEST_PERIOD_MS) {
    this->last_request_ms_ = now;
    this->queue_request_();
    this->send_pending_chunk_();
  }

  if (this->online_ && now - this->last_fast_ms_ > ONLINE_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Plus de télémétrie depuis %u ms", (unsigned) ONLINE_TIMEOUT_MS);
    this->set_online_(false);
  }

  if (now - this->last_publish_ms_ >= this->publish_interval_) {
    this->last_publish_ms_ = now;
    this->publish_averages_();
  }
}

void InMotionV1::queue_request_() {
  switch (this->phase_) {
    case Phase::PASSWORD:
      if (this->password_tries_ < PASSWORD_MAX_TRIES) {
        this->password_tries_++;
        ESP_LOGD(TAG, "Mot de passe (%d/%d)", this->password_tries_, PASSWORD_MAX_TRIES);
        this->tx_ = inmotion::password_request(this->password_.c_str());
        break;
      }
      ESP_LOGW(TAG, "Pas de réponse au mot de passe, on continue quand même");
      this->phase_ = Phase::SLOW_INFO;
      [[fallthrough]];
    case Phase::SLOW_INFO:
      this->tx_ = inmotion::slow_info_request();
      break;
    case Phase::FAST_INFO:
      this->tx_ = inmotion::fast_info_request();
      break;
    case Phase::IDLE:
      return;
  }
  this->tx_pos_ = 0;
  ESP_LOGV(TAG, "TX %s", format_hex_pretty(this->tx_).c_str());
}

void InMotionV1::send_pending_chunk_() {
  size_t n = std::min(CHUNK_SIZE, this->tx_.size() - this->tx_pos_);
  if (n == 0)
    return;
  auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                      this->write_handle_, n, this->tx_.data() + this->tx_pos_,
                                      ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Écriture FFE9 échouée : %d", err);
    this->tx_pos_ = this->tx_.size();  // on abandonne cette requête, la suivante partira dans 250 ms
    return;
  }
  this->tx_pos_ += n;
  this->last_chunk_ms_ = millis();
}

void InMotionV1::handle_message_(const inmotion::Message &m) {
  switch (m.id) {
    case inmotion::ID_PIN_CODE:
      if (this->phase_ == Phase::PASSWORD) {
        ESP_LOGI(TAG, "Mot de passe accepté");
        this->phase_ = Phase::SLOW_INFO;
      }
      break;
    case inmotion::ID_SLOW_INFO: {
      if (m.ex_data.empty())
        break;
      int model = inmotion::parse_model_id(m.ex_data);
      if (model == MODEL_V10F) {
        ESP_LOGI(TAG, "Modèle 141 (V10F) identifié");
      } else {
        ESP_LOGW(TAG, "Modèle %d ≠ 141 (V10F) : les offsets peuvent différer", model);
      }
      this->phase_ = Phase::FAST_INFO;
      break;
    }
    case inmotion::ID_FAST_INFO: {
      inmotion::FastInfo f;
      if (inmotion::parse_fast(m.ex_data, f))
        this->handle_fast_(f);
      break;
    }
    case inmotion::ID_ALERT:
      ESP_LOGW(TAG, "Alerte roue : %s", format_hex_pretty(m.data, 8).c_str());
      break;
    default:
      break;
  }
}

void InMotionV1::handle_fast_(const inmotion::FastInfo &f) {
  this->last_fast_ms_ = millis();
  ESP_LOGV(TAG, "%.2f V %+.2f A mode=0x%X", f.voltage, f.current, (unsigned) f.mode_raw);

  if (this->voltage_filter_.accept(f.voltage)) {
    this->voltage_sum_ += f.voltage;
    this->voltage_count_++;
  } else {
    this->rejected_voltage_++;
    ESP_LOGD(TAG, "Tension ignorée : %.2f V (%u rejets au total)", f.voltage, (unsigned) this->rejected_voltage_);
    return;  // trame d'extinction : on n'en croit pas non plus le mode
  }
  this->current_sum_ += f.current;
  this->current_count_++;
  this->last_temperature_ = f.temperature;
  this->have_temperature_ = true;

  this->set_online_(true);
  if (this->charging_sensor_ != nullptr)
    this->charging_sensor_->publish_state(f.charging());
}

void InMotionV1::publish_averages_() {
  if (this->voltage_count_ > 0) {
    float v = this->voltage_sum_ / this->voltage_count_;
    if (this->voltage_sensor_ != nullptr)
      this->voltage_sensor_->publish_state(v);
    if (this->battery_level_sensor_ != nullptr)
      this->battery_level_sensor_->publish_state(this->better_percents_ ? inmotion::percent_better(v)
                                                                        : inmotion::percent_standard(v));
  }
  if (this->current_count_ > 0 && this->current_sensor_ != nullptr)
    this->current_sensor_->publish_state(this->current_sum_ / this->current_count_);
  if (this->have_temperature_ && this->temperature_sensor_ != nullptr)
    this->temperature_sensor_->publish_state(this->last_temperature_);

  this->voltage_sum_ = this->current_sum_ = 0;
  this->voltage_count_ = this->current_count_ = 0;
}

void InMotionV1::set_online_(bool online) {
  if (online == this->online_)
    return;
  this->online_ = online;
  ESP_LOGI(TAG, "Roue %s", online ? "en ligne" : "hors ligne");
  if (this->online_sensor_ != nullptr)
    this->online_sensor_->publish_state(online);
  // Hors ligne, on ne sait plus si la roue charge : on publie « non » et
  // Home Assistant s'appuie sur « en ligne » pour interpréter cet état.
  if (!online && this->charging_sensor_ != nullptr)
    this->charging_sensor_->publish_state(false);
  if (!online)
    this->voltage_filter_.reset();
}

}  // namespace esphome::inmotion_v1

#endif  // USE_ESP32
