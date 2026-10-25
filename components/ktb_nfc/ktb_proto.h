#pragma once
// Reine Protokoll-Logik (keine ESPHome-Abhaengigkeit, auf dem PC testbar):
// SHA-256 / HMAC-SHA-256, PN532-Frames, KTB-APDUs.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace ktb {

// ---------------------------------------------------------------- SHA-256
class Sha256 {
 public:
  Sha256() { reset(); }
  void reset() {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(h_, iv, sizeof(h_));
    len_ = 0;
    fill_ = 0;
  }
  void update(const uint8_t *d, size_t n) {
    len_ += n;
    while (n > 0) {
      size_t c = 64 - fill_;
      if (c > n) c = n;
      memcpy(buf_ + fill_, d, c);
      fill_ += c;
      d += c;
      n -= c;
      if (fill_ == 64) {
        block_(buf_);
        fill_ = 0;
      }
    }
  }
  void final(uint8_t out[32]) {
    uint64_t bits = len_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t z = 0;
    while (fill_ != 56) update(&z, 1);
    uint8_t lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    update(lb, 8);
    for (int i = 0; i < 8; i++) {
      out[i * 4] = (uint8_t)(h_[i] >> 24);
      out[i * 4 + 1] = (uint8_t)(h_[i] >> 16);
      out[i * 4 + 2] = (uint8_t)(h_[i] >> 8);
      out[i * 4 + 3] = (uint8_t)(h_[i]);
    }
  }

 private:
  static uint32_t ror_(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void block_(const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ror_(w[i - 15], 7) ^ ror_(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ror_(w[i - 2], 17) ^ ror_(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = ror_(e, 6) ^ ror_(e, 11) ^ ror_(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = h + S1 + ch + K[i] + w[i];
      uint32_t S0 = ror_(a, 2) ^ ror_(a, 13) ^ ror_(a, 22);
      uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + mj;
      h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
  }
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t fill_;
  uint64_t len_;
};

inline void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]) {
  uint8_t k[64];
  memset(k, 0, sizeof(k));
  if (klen > 64) {
    Sha256 s;
    s.update(key, klen);
    s.final(k);
  } else {
    memcpy(k, key, klen);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  uint8_t inner[32];
  Sha256 s;
  s.update(ipad, 64);
  s.update(msg, mlen);
  s.final(inner);
  s.reset();
  s.update(opad, 64);
  s.update(inner, 32);
  s.final(out);
}

inline bool ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

// ---------------------------------------------------------------- Hilfen
inline std::string hex_str(const uint8_t *d, size_t n, char sep = 0) {
  static const char *H = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    if (sep && i) s += sep;
    s += H[d[i] >> 4];
    s += H[d[i] & 15];
  }
  return s;
}

inline bool parse_hex(const std::string &s, uint8_t *out, size_t n) {
  if (s.size() != n * 2) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < n; i++) {
    int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi * 16 + lo);
  }
  return true;
}

// ---------------------------------------------------------------- PN532-Frames
// Baut ein Host->PN532-Frame: 00 00 FF LEN LCS D4 <cmd...> DCS 00
inline std::vector<uint8_t> build_frame(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> f;
  uint8_t len = (uint8_t)(cmd.size() + 1);
  f.push_back(0x00);
  f.push_back(0x00);
  f.push_back(0xFF);
  f.push_back(len);
  f.push_back((uint8_t)(~len + 1));
  f.push_back(0xD4);
  uint8_t sum = 0xD4;
  for (uint8_t b : cmd) {
    f.push_back(b);
    sum += b;
  }
  f.push_back((uint8_t)(~sum + 1));
  f.push_back(0x00);
  return f;
}

inline bool is_ack(const uint8_t *b, size_t n) {
  static const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  return n >= 6 && memcmp(b, ACK, 6) == 0;
}

// Prueft ein PN532->Host-Frame (TFI D5) und liefert die Nutzdaten nach dem Antwort-Code.
inline bool parse_response(const uint8_t *b, size_t n, uint8_t expect_resp_code, std::vector<uint8_t> &payload) {
  payload.clear();
  size_t i = 0;
  while (i + 2 < n && !(b[i] == 0x00 && b[i + 1] == 0x00 && b[i + 2] == 0xFF)) i++;
  if (i + 2 >= n) return false;
  i += 3;
  if (i + 2 > n) return false;
  uint8_t len = b[i], lcs = b[i + 1];
  if ((uint8_t)(len + lcs) != 0) return false;
  if (len < 2) return false;
  size_t start = i + 2;
  if (start + len + 1 > n) return false;
  if (b[start] != 0xD5 || b[start + 1] != expect_resp_code) return false;
  uint8_t sum = 0;
  for (size_t k = 0; k < len; k++) sum += b[start + k];
  if ((uint8_t)(sum + b[start + len]) != 0) return false;
  payload.assign(b + start + 2, b + start + len);
  return true;
}

// ---------------------------------------------------------------- KTB-APDUs
static const uint8_t KTB_AID[8] = {0xF0, 0x4B, 0x54, 0x42, 0x4E, 0x46, 0x43, 0x31};
static const char KTB_DOMAIN[] = "KTB-NFC-v1";

inline std::vector<uint8_t> select_apdu() {
  std::vector<uint8_t> a = {0x00, 0xA4, 0x04, 0x00, 0x08};
  a.insert(a.end(), KTB_AID, KTB_AID + 8);
  a.push_back(0x00);
  return a;  // 14 Byte
}

inline std::vector<uint8_t> challenge_apdu(const uint8_t ch[16]) {
  std::vector<uint8_t> a = {0x80, 0x10, 0x00, 0x00, 0x10};
  a.insert(a.end(), ch, ch + 16);
  a.push_back(0x00);
  return a;  // 22 Byte
}

// Erwartete Antwort: HMAC-SHA256(key, "KTB-NFC-v1" || challenge)
inline void expected_mac(const uint8_t key[32], const uint8_t ch[16], uint8_t out[32]) {
  uint8_t msg[10 + 16];
  memcpy(msg, KTB_DOMAIN, 10);
  memcpy(msg + 10, ch, 16);
  hmac_sha256(key, 32, msg, sizeof(msg), out);
}

}  // namespace ktb
