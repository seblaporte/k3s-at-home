#pragma once
// Protocole InMotion « V1 » (V10F, etc.), porté depuis inmotion_v10f.py.
// Aucune dépendance ESPHome : ce fichier est aussi compilé par test/test_protocol.cpp.

#include <cstdint>
#include <cstring>
#include <vector>

namespace inmotion {

constexpr uint32_t ID_FAST_INFO = 0x0F550113;
constexpr uint32_t ID_SLOW_INFO = 0x0F550114;
constexpr uint32_t ID_PIN_CODE = 0x0F550307;
constexpr uint32_t ID_ALERT = 0x0F780101;

constexpr uint8_t TYPE_DATA = 0;
constexpr uint8_t TYPE_REMOTE = 1;
constexpr uint8_t LEN_EXTENDED = 0xFE;
constexpr size_t MAX_FRAME = 512;

inline uint32_t u32le(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

struct Message {
  uint32_t id{0};
  uint8_t data[8]{};
  uint8_t len{0};
  uint8_t ch{0};
  uint8_t format{0};
  uint8_t type{0};
  std::vector<uint8_t> ex_data;  // vide si trame non étendue
};

// Trame prête à écrire sur FFE9 : AA AA | corps échappé | checksum | 55 55
inline std::vector<uint8_t> encode(uint32_t id, const uint8_t data[8], uint8_t type) {
  uint8_t body[16];
  for (int i = 0; i < 4; i++)
    body[i] = (id >> (8 * i)) & 0xFF;
  memcpy(body + 4, data, 8);
  body[12] = 8;  // len
  body[13] = 5;  // ch
  body[14] = 0;  // format standard
  body[15] = type;

  std::vector<uint8_t> out{0xAA, 0xAA};
  uint8_t check = 0;
  for (uint8_t c : body) {
    check += c;
    if (c == 0xAA || c == 0x55 || c == 0xA5)
      out.push_back(0xA5);
    out.push_back(c);
  }
  out.push_back(check);  // WheelLog n'échappe pas le checksum
  out.push_back(0x55);
  out.push_back(0x55);
  return out;
}

inline std::vector<uint8_t> password_request(const char *password) {
  uint8_t d[8]{};
  memcpy(d, password, 6);
  return encode(ID_PIN_CODE, d, TYPE_DATA);
}

inline std::vector<uint8_t> slow_info_request() {
  uint8_t d[8];
  memset(d, 0xFF, 8);
  return encode(ID_SLOW_INFO, d, TYPE_REMOTE);
}

inline std::vector<uint8_t> fast_info_request() {
  uint8_t d[8];
  memset(d, 0xFF, 8);
  return encode(ID_FAST_INFO, d, TYPE_DATA);
}

// Réassemble les trames octet par octet. La fin est déterminée par la longueur.
class Unpacker {
 public:
  // Renvoie true quand `out` contient une trame complète au checksum valide.
  bool feed(uint8_t c, Message &out) {
    if (!collecting_) {
      if (c == 0xAA && prev_ == 0xAA) {
        reset_();
        collecting_ = true;
      } else {
        prev_ = c;
      }
      return false;
    }
    if (escape_) {
      escape_ = false;
    } else if (c == 0xA5 && !at_checksum_()) {
      escape_ = true;
      return false;
    }
    buf_.push_back(c);

    size_t expected = expected_size_();
    if (expected == 0)
      return false;
    if (expected > MAX_FRAME) {
      reset_();
      return false;
    }
    if (buf_.size() < expected)
      return false;

    bool ok = finish_(out);
    reset_();
    return ok;
  }

  uint32_t bad_checksum{0};

 private:
  size_t expected_size_() const {
    if (buf_.size() < 16)
      return 0;
    if (buf_[12] == LEN_EXTENDED)
      return 16 + u32le(&buf_[4]) + 3;
    return 16 + 3;
  }
  bool at_checksum_() const {
    size_t e = expected_size_();
    return e != 0 && buf_.size() + 3 == e;
  }
  bool finish_(Message &m) {
    size_t n = buf_.size();
    if (buf_[n - 1] != 0x55 || buf_[n - 2] != 0x55)
      return false;
    uint8_t check = 0;
    for (size_t i = 0; i < n - 3; i++)
      check += buf_[i];
    if (check != buf_[n - 3]) {
      bad_checksum++;
      return false;
    }
    m.id = u32le(&buf_[0]);
    memcpy(m.data, &buf_[4], 8);
    m.len = buf_[12];
    m.ch = buf_[13];
    m.format = buf_[14];
    m.type = buf_[15];
    m.ex_data.clear();
    if (m.len == LEN_EXTENDED)
      m.ex_data.assign(buf_.begin() + 16, buf_.end() - 3);
    return true;
  }
  void reset_() {
    collecting_ = false;
    escape_ = false;
    prev_ = -1;
    buf_.clear();
  }

  bool collecting_{false};
  bool escape_{false};
  int prev_{-1};
  std::vector<uint8_t> buf_;
};

struct FastInfo {
  float voltage;   // V
  float current;   // A (positif = la roue consomme)
  int8_t temperature;  // °C (octet 32 ; l'octet 34 est incohérent sur V10F 102.3.1)
  uint32_t mode_raw;   // >>4 : 1 arrêt, 2 conduite, 3 charge ; &0xF == 1 : moteur coupé
  bool charging() const { return (mode_raw >> 4) == 3; }
};

inline bool parse_fast(const std::vector<uint8_t> &ex, FastInfo &f) {
  if (ex.size() < 64)
    return false;
  f.voltage = u32le(&ex[24]) / 100.0f;
  f.current = int32_t(u32le(&ex[20])) / 100.0f;
  f.temperature = int8_t(ex[32]);
  f.mode_raw = u32le(&ex[60]);
  return true;
}

// Identifiant modèle WheelLog : str(ex[107]) si > 0, puis str(ex[104]). V10F = 141.
inline int parse_model_id(const std::vector<uint8_t> &ex) {
  if (ex.size() < 108)
    return -1;
  int hi = int8_t(ex[107]), lo = int8_t(ex[104]);
  return hi > 0 ? hi * (lo >= 10 ? 100 : 10) + lo : lo;
}

inline float percent_standard(float v) {
  if (v > 82.5f)
    return 100.0f;
  if (v > 68.0f)
    return (v - 68.0f) / 14.5f * 100.0f;
  return 0.0f;
}

inline float percent_better(float v) {
  if (v > 83.5f)
    return 100.0f;
  if (v > 68.0f)
    return (v - 66.5f) / 17.0f * 100.0f;
  if (v > 64.0f)
    return (v - 64.0f) / 45.0f * 100.0f;
  return 0.0f;
}

// Rejette les mesures aberrantes. À l'extinction, la roue envoie encore
// une ou deux trames avec une tension qui s'effondre (ex. 78,1 → 69,8 → 45,9 V).
class VoltageFilter {
 public:
  static constexpr float MIN_V = 60.0f;
  static constexpr float MAX_V = 85.0f;
  static constexpr float MAX_STEP_V = 2.0f;   // bruit observé : ±0,4 V
  static constexpr int REBASE_AFTER = 8;      // ~2 s de valeurs cohérentes entre elles → nouvelle référence

  bool accept(float v) {
    if (v < MIN_V || v > MAX_V)
      return false;
    if (last_ < 0 || (v > last_ - MAX_STEP_V && v < last_ + MAX_STEP_V)) {
      last_ = v;
      rejected_ = 0;
      return true;
    }
    // Saut réel (ex. reconnexion après une longue charge) : on l'accepte s'il persiste
    if (++rejected_ >= REBASE_AFTER) {
      last_ = v;
      rejected_ = 0;
      return true;
    }
    return false;
  }
  void reset() {
    last_ = -1;
    rejected_ = 0;
  }

 private:
  float last_{-1};
  int rejected_{0};
};

}  // namespace inmotion
