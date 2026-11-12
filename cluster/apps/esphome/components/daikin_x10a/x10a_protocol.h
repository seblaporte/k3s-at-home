#pragma once

// Protocole « I » du port X10A des pompes à chaleur Daikin Altherma et conversions des valeurs,
// repris d'ESPAltherma (https://github.com/raomin/ESPAltherma, licence MIT) : comm.h et
// converters.h. Sans dépendance à ESPHome pour être testable sur la machine hôte.
//
// Écarts volontaires avec ESPAltherma (bugs corrigés) :
//   - 401…406 (pression → température R32) : il manquait les `break`, la valeur finissait en
//     « Conv not avail. ».
//   - 211 : comparait le pointeur à 0, la branche « OFF » n'était jamais prise.
//   - 215/216 : sprintf("{0:X}{1:X}") hérité du C#, réécrit en "%X%X".
//   - 217 : index du tableau non borné.
// Les valeurs « --- » d'ESPAltherma deviennent NAN (état « inconnu » dans Home Assistant).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace esphome::daikin_x10a::x10a {

static const uint8_t ERROR_REPLY_0 = 0x15;
static const uint8_t ERROR_REPLY_1 = 0xEA;
// Réponse : 0x40, registre, longueur restante, données…, CRC
static const size_t HEADER_LEN = 3;

enum class Kind { UNSUPPORTED, NUMBER, TEXT, BINARY };

inline uint8_t crc(const uint8_t *src, size_t len) {
  uint8_t b = 0;
  for (size_t i = 0; i < len; i++)
    b += src[i];
  return ~b;
}

// Requête de lecture d'un registre : 0x03 0x40 <registre> <CRC>
inline void build_query(uint8_t reg, uint8_t out[4]) {
  out[0] = 0x03;
  out[1] = 0x40;
  out[2] = reg;
  out[3] = crc(out, 3);
}

inline Kind kind_of(int convid) {
  if ((convid >= 101 && convid <= 119) || (convid >= 151 && convid <= 158) || (convid >= 161 && convid <= 165) ||
      convid == 211 || convid == 312 || (convid >= 401 && convid <= 406))
    return Kind::NUMBER;
  if (convid == 100 || convid == 201 || convid == 203 || convid == 204 || convid == 215 || convid == 216 ||
      convid == 217 || convid == 315 || convid == 316)
    return Kind::TEXT;
  if (convid == 200 || (convid >= 300 && convid <= 307))
    return Kind::BINARY;
  return Kind::UNSUPPORTED;
}

// cnvflg 0 : petit-boutiste, 1 : gros-boutiste
inline uint16_t get_unsigned(const uint8_t *d, int size, int cnvflg) {
  if (size == 1)
    return d[0];
  if (cnvflg == 0)
    return (uint16_t) ((d[1] << 8) | d[0]);
  return (uint16_t) ((d[0] << 8) | d[1]);
}

// Comme ESPAltherma, une valeur sur 1 octet n'est pas étendue en signe
inline int16_t get_signed(const uint8_t *d, int size, int cnvflg) { return (int16_t) get_unsigned(d, size, cnvflg); }

// Courbe de saturation du R32
inline double press_to_temp(double p) {
  return -2.6989493795556E-07 * std::pow(p, 6) + 4.26383417104661E-05 * std::pow(p, 5) -
         0.00262978346547749 * std::pow(p, 4) + 0.0805858127503585 * std::pow(p, 3) - 1.31924457284073 * p * p +
         13.4157368435437 * p - 51.1813342993155;
}

inline float to_number(int convid, const uint8_t *d, int size) {
  switch (convid) {
    case 101: return get_signed(d, size, 0);
    case 102: return get_signed(d, size, 1);
    case 103: return get_signed(d, size, 0) / 256.0;
    case 104: return get_signed(d, size, 1) / 256.0;
    case 105: return get_signed(d, size, 0) * 0.1;
    case 106: return get_signed(d, size, 1) * 0.1;
    case 107: {
      int16_t v = get_signed(d, size, 0);
      return v == -32768 ? NAN : v * 0.1;
    }
    case 108: {
      int16_t v = get_signed(d, size, 1);
      return v == -32768 ? NAN : v * 0.1;
    }
    case 109: return get_signed(d, size, 0) / 256.0 * 2.0;
    case 110: return get_signed(d, size, 1) / 256.0 * 2.0;
    case 111: return get_signed(d, size, 1) * 0.5;
    case 112: return (get_signed(d, size, 1) - 64) * 0.5;
    case 113: return get_signed(d, size, 1) * 0.25;
    case 114: {
      if (d[0] == 0 && d[1] == 128)
        return NAN;
      uint16_t n = (uint16_t) (d[1] * 256) | d[0];
      bool negative = (d[1] & 128) != 0;
      if (negative)
        n = ~(n - 1);
      double v = ((n & 0xFF00) / 256) + (n & 0xFF) / 256.0;
      v *= 10.0;
      return negative ? -v : v;
    }
    case 115: return get_signed(d, size, 0) / 2560.0;
    case 116: return get_signed(d, size, 1) / 2560.0;
    case 117: return get_signed(d, size, 0) * 0.01;
    case 118: return get_signed(d, size, 1) * 0.01;
    case 119: {
      if (d[0] == 0 && d[1] == 128)
        return NAN;
      uint16_t n = (uint16_t) (d[1] * 256) | (d[0] & 127);
      return ((n & 0xFF00) / 256) + (n & 0xFF) / 256.0;
    }
    case 151: return get_unsigned(d, size, 0);
    case 152: return get_unsigned(d, size, 1);
    case 153: return get_unsigned(d, size, 0) / 256.0;
    case 154: return get_unsigned(d, size, 1) / 256.0;
    case 155: return get_unsigned(d, size, 0) * 0.1;
    case 156: return get_unsigned(d, size, 1) * 0.1;
    case 157: return get_unsigned(d, size, 0) / 256.0 * 2.0;
    case 158: return get_unsigned(d, size, 1) / 256.0 * 2.0;
    case 161: return get_unsigned(d, size, 1) * 0.5;
    case 162: return (get_unsigned(d, size, 1) - 64) * 0.5;
    case 163: return get_unsigned(d, size, 1) * 0.25;
    case 164: return get_unsigned(d, size, 1) * 5;
    case 165: return get_unsigned(d, size, 0) & 16383;
    case 211: return d[0];
    case 312: {
      double v = ((7 & (d[0] >> 4)) + (15 & d[0])) / 16.0;
      return (d[0] & 128) ? -v : v;
    }
    case 401: return press_to_temp(get_signed(d, size, 0));
    case 402: return press_to_temp(get_signed(d, size, 1));
    case 403: return press_to_temp(get_signed(d, size, 0) / 256.0);
    case 404: return press_to_temp(get_signed(d, size, 1) / 256.0);
    case 405: return press_to_temp(get_signed(d, size, 0) * 0.1);
    case 406: return press_to_temp(get_signed(d, size, 1) * 0.1);
    default: return NAN;
  }
}

inline bool to_binary(int convid, const uint8_t *d) {
  if (convid == 200)
    return d[0] != 0;
  return (d[0] >> (convid % 10)) & 1;
}

inline std::string to_text(int convid, const uint8_t *d, int size) {
  switch (convid) {
    case 100: {
      std::string s;
      for (int i = 0; i < size && d[i] != 0; i++)
        s += (char) d[i];
      return s;
    }
    case 203: {
      static const char *const T[] = {"Normal", "Error", "Warning", "Caution"};
      return d[0] < 4 ? T[d[0]] : "-";
    }
    case 204: {
      static const char A[] = " ACEHFJLPU987654";
      static const char B[] = "0123456789AHCJEF";
      return std::string{A[(d[0] >> 4) & 15], B[d[0] & 15]};
    }
    case 215:
    case 216: {
      char buf[3];
      snprintf(buf, sizeof(buf), "%X%X", d[0] >> 4, d[0] & 15);
      return buf;
    }
    case 201:
    case 217: {
      static const char *const T[] = {
          "Fan Only",         "Heating",          "Cooling",          "Auto",
          "Ventilation",      "Auto Cool",        "Auto Heat",        "Dry",
          "Aux.",             "Cooling Storage",  "Heating Storage",  "UseStrdThrm(cl)1",
          "UseStrdThrm(cl)2", "UseStrdThrm(cl)3", "UseStrdThrm(cl)4", "UseStrdThrm(ht)1",
          "UseStrdThrm(ht)2", "UseStrdThrm(ht)3", "UseStrdThrm(ht)4"};
      return d[0] < sizeof(T) / sizeof(T[0]) ? T[d[0]] : "-";
    }
    case 315: {
      static const char *const T[] = {"Stop", "Heating", "Cooling", "??", "DHW", "Heating + DHW", "Cooling + DHW"};
      uint8_t b = d[0] >> 4;
      return b < 7 ? T[b] : "-";
    }
    case 316: {
      static const char *const T[] = {"H/P only", "Hybrid", "Boiler only"};
      uint8_t b = d[0] >> 4;
      return b < 3 ? T[b] : "Unknown";
    }
    default: return "";
  }
}

}  // namespace esphome::daikin_x10a::x10a
