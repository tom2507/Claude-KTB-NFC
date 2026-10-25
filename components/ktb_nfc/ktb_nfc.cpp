#include "ktb_nfc.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace ktb_nfc {

static const char *const TAG = "ktb_nfc";

// PN532-Befehlscodes
static const uint8_t CMD_GET_FIRMWARE = 0x02;
static const uint8_t CMD_RF_CONFIG = 0x32;
static const uint8_t CMD_SAM_CONFIG = 0x14;
static const uint8_t CMD_IN_LIST_PASSIVE = 0x4A;
static const uint8_t CMD_IN_DATA_EXCHANGE = 0x40;

static const uint32_t COOLDOWN_PHONE_MS = 3000;
static const uint32_t CS_SETUP_MS = 2;          // PN532 braucht CS >= 2 ms low vor dem ersten SPI-Byte
static const uint32_t INIT_RETRY_MS = 5000;
// Revision 3: Nur Diagnose, keine Aenderung der NFC-Kommandos oder Zeitlimits.
// Diese Zaehler gelten fuer die eine KTB-NFC-Teststation.
static uint32_t diag_scans = 0;
static uint32_t diag_empty = 0;
static uint32_t diag_targets = 0;
static uint32_t diag_scan_errors = 0;
static uint32_t diag_last_summary = 0;

// ------------------------------------------------------------------ SPI-Ebene
void KtbNfc::write_frame_(const std::vector<uint8_t> &frame, uint32_t wake_ms) {
  this->enable();
  delay(wake_ms > 0 ? wake_ms : CS_SETUP_MS);  // CS vor dem ersten Byte low halten (beim Wecken: laenger)
  this->write_byte(0x01);           // PN532-SPI: "Data write"
  this->write_array(frame.data(), frame.size());
  this->disable();
}

void KtbNfc::read_raw_(uint8_t *buf, size_t len) {
  this->enable();
  delay(CS_SETUP_MS);
  this->write_byte(0x03);  // PN532-SPI: "Data read"
  this->read_array(buf, len);
  this->disable();
}

bool KtbNfc::is_ready_() {
  this->enable();
  delay(CS_SETUP_MS);
  this->write_byte(0x02);  // PN532-SPI: "Status read"
  uint8_t s = this->read_byte();
  this->disable();
  return (s & 0x01) != 0;
}

bool KtbNfc::wait_ready_(uint32_t timeout_ms) {
  uint32_t start = millis();
  while (true) {
    if (this->is_ready_()) return true;
    if (millis() - start > timeout_ms) return false;
    delay(1);
  }
}

// ------------------------------------------------------------------ PN532-Befehle
bool KtbNfc::send_command_(const std::vector<uint8_t> &cmd) {
  this->write_frame_(ktb::build_frame(cmd));
  if (!this->wait_ready_(100)) {
    ESP_LOGW(TAG, "Kein ACK vom PN532 (Befehl 0x%02X)", (unsigned) (cmd.empty() ? 0 : cmd[0]));
    return false;
  }
  uint8_t ack[6];
  this->read_raw_(ack, sizeof(ack));
  if (!ktb::is_ack(ack, sizeof(ack))) {
    ESP_LOGW(TAG, "Ungueltiges ACK: %s", ktb::hex_str(ack, sizeof(ack), ' ').c_str());
    return false;
  }
  return true;
}

bool KtbNfc::recv_response_(uint8_t resp_code, std::vector<uint8_t> &payload) {
  uint8_t buf[64];
  this->read_raw_(buf, sizeof(buf));
  if (!ktb::parse_response(buf, sizeof(buf), resp_code, payload)) {
    ESP_LOGW(TAG, "Antwort nicht lesbar (erwartet 0x%02X): %s", (unsigned) resp_code, ktb::hex_str(buf, 24, ' ').c_str());
    return false;
  }
  return true;
}

bool KtbNfc::exec_(const std::vector<uint8_t> &cmd, uint8_t resp_code, std::vector<uint8_t> &payload,
                   uint32_t timeout_ms) {
  uint32_t diag_start = millis();
  if (!this->send_command_(cmd)) {
    ESP_LOGW(TAG, "DIAG exec cmd=0x%02X ACK fehlgeschlagen nach %u ms", (unsigned) cmd[0],
             (unsigned) (millis() - diag_start));
    return false;
  }
  if (!this->wait_ready_(timeout_ms)) {
    ESP_LOGW(TAG, "Timeout auf Antwort (Befehl 0x%02X)", (unsigned) cmd[0]);
    ESP_LOGW(TAG, "DIAG exec cmd=0x%02X Antwort-Timeout nach %u ms", (unsigned) cmd[0],
             (unsigned) (millis() - diag_start));
    return false;
  }
  bool diag_ok = this->recv_response_(resp_code, payload);
  if (cmd[0] == CMD_IN_DATA_EXCHANGE || !diag_ok)
    ESP_LOGD(TAG, "DIAG exec cmd=0x%02X ok=%u dauer=%u ms payload=%u B", (unsigned) cmd[0],
             (unsigned) diag_ok, (unsigned) (millis() - diag_start), (unsigned) payload.size());
  return diag_ok;
}

void KtbNfc::abort_command_() {
  // Ein ACK-Frame vom Host bricht den laufenden PN532-Befehl ab.
  this->write_frame_({0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00});
  delay(5);
}

void KtbNfc::rf_off_() {
  std::vector<uint8_t> p;
  this->exec_({CMD_RF_CONFIG, 0x01, 0x00}, 0x33, p, 100);
}

// ------------------------------------------------------------------ Component
void KtbNfc::set_key_hex(const std::string &hex) { this->key_valid_ = ktb::parse_hex(hex, this->key_, 32); }

bool KtbNfc::init_pn532_() {
  // PN532 aus dem Power-Down wecken: CS >= 10 ms low, dann Dummy-Befehl. ACK und Antwort werden verworfen.
  this->write_frame_(ktb::build_frame({CMD_GET_FIRMWARE}), 10);
  delay(30);
  uint8_t drain[64];
  for (int i = 0; i < 2 && this->is_ready_(); i++) this->read_raw_(drain, sizeof(drain));
  delay(10);

  std::vector<uint8_t> p;
  if (!this->exec_({CMD_GET_FIRMWARE}, 0x03, p, 200) || p.size() < 4) return false;
  ESP_LOGI(TAG, "PN532 gefunden: IC=0x%02X Firmware %u.%u", (unsigned) p[0], (unsigned) p[1], (unsigned) p[2]);

  // SAM: Normal mode, Timeout 0x14, IRQ aus Statusbyte
  if (!this->exec_({CMD_SAM_CONFIG, 0x01, 0x14, 0x01}, 0x15, p, 200)) {
    ESP_LOGW(TAG, "SAMConfiguration fehlgeschlagen");
    return false;
  }
  // Revision 4: 16 statt 3 passive Aktivierungsversuche (einziger Funktionstest).
  // RF-Zustand, HMAC, APDUs und alle Timeouts bleiben unveraendert.
  this->exec_({CMD_RF_CONFIG, 0x05, 0xFF, 0x01, 0x10}, 0x33, p, 100);
  ESP_LOGI(TAG, "PN532 bereit - warte auf Tags / Handy");
  return true;
}

void KtbNfc::setup() {
  this->spi_setup();
  if (!this->key_valid_) {
    ESP_LOGE(TAG, "Schluessel fehlt oder ist kein 64-stelliger Hex-String");
    this->mark_failed();
    return;
  }
  this->pn_ready_ = this->init_pn532_();
  if (!this->pn_ready_) {
    ESP_LOGW(TAG, "PN532 antwortet noch nicht - neuer Versuch alle %u s", (unsigned) (INIT_RETRY_MS / 1000));
    this->next_init_ = millis() + INIT_RETRY_MS;
  }
}

void KtbNfc::dump_config() {
  ESP_LOGCONFIG(TAG, "KTB NFC Key Reader (PN532 per SPI, APDU/HMAC-Challenge)");
  ESP_LOGCONFIG(TAG, "  PN532 bereit: %s", this->pn_ready_ ? "ja" : "nein");
  LOG_UPDATE_INTERVAL(this);
}

void KtbNfc::update() {
  if (!this->pn_ready_) {
    if ((int32_t) (millis() - this->next_init_) < 0) return;
    this->pn_ready_ = this->init_pn532_();
    if (!this->pn_ready_) {
      ESP_LOGW(TAG, "PN532 antwortet nicht (Verkabelung / SPI-Pins / Stromversorgung pruefen) - naechster Versuch in %u s",
               (unsigned) (INIT_RETRY_MS / 1000));
      this->next_init_ = millis() + INIT_RETRY_MS;
    }
    return;
  }
  if (this->state_ != State::IDLE) return;
  if ((int32_t)(millis() - this->cooldown_until_) < 0) return;
  if (millis() - diag_last_summary >= 10000) {
    ESP_LOGI(TAG, "DIAG 10s+ scans=%u leer=%u ziele=%u scanfehler=%u status=%s",
             (unsigned) diag_scans, (unsigned) diag_empty, (unsigned) diag_targets,
             (unsigned) diag_scan_errors, this->pn_ready_ ? "bereit" : "nicht bereit");
    diag_scans = diag_empty = diag_targets = diag_scan_errors = 0;
    diag_last_summary = millis();
  }
  // 1 Ziel, 106 kbps Typ A
  ++diag_scans;
  if (!this->send_command_({CMD_IN_LIST_PASSIVE, 0x01, 0x00})) {
    ++diag_scan_errors;
    ESP_LOGW(TAG, "DIAG Scan-Befehl fehlgeschlagen");
    return;
  }
  this->state_ = State::WAIT_LIST;
  this->state_ts_ = millis();
}

void KtbNfc::loop() {
  if (!this->pn_ready_ || this->state_ != State::WAIT_LIST) return;
  if (this->is_ready_()) {
    this->state_ = State::IDLE;
    std::vector<uint8_t> p;
    if (!this->recv_response_(0x4B, p)) {
      ++diag_scan_errors;
      ESP_LOGW(TAG, "DIAG Scan-Antwort ungueltig nach %u ms", (unsigned) (millis() - this->state_ts_));
      this->rf_off_();
      return;
    }
    if (p.empty() || p[0] == 0) ++diag_empty;
    else ++diag_targets;
    if (!p.empty() && p[0] != 0)
      ESP_LOGD(TAG, "DIAG Ziel gefunden nach %u ms, Antwort=%u B", (unsigned) (millis() - this->state_ts_),
               (unsigned) p.size());
    this->handle_list_response_(p);
  } else if (millis() - this->state_ts_ > 1500) {
    ++diag_scan_errors;
    ESP_LOGW(TAG, "InListPassiveTarget Timeout - breche ab");
    ESP_LOGW(TAG, "DIAG Scan-Timeout nach %u ms", (unsigned) (millis() - this->state_ts_));
    this->abort_command_();
    this->rf_off_();
    this->state_ = State::IDLE;
  }
}

// ------------------------------------------------------------------ Logik
void KtbNfc::handle_list_response_(const std::vector<uint8_t> &p) {
  // p: NbTg, [Tg, ATQA(2), SAK, NFCIDLen, NFCID..., (ATS...)]
  if (p.empty() || p[0] == 0) {
    this->last_uid_.clear();
    return;  // kein Tag im Feld, RF bleibt an
  }
  if (p.size() < 6) {
    ESP_LOGW(TAG, "Zielantwort zu kurz");
    this->rf_off_();
    return;
  }
  uint8_t tg = p[1];
  uint8_t sak = p[4];
  uint8_t uid_len = p[5];
  if (uid_len == 0 || p.size() < (size_t) (6 + uid_len)) {
    ESP_LOGW(TAG, "UID-Laenge unplausibel");
    this->rf_off_();
    return;
  }
  std::string uid = ktb::hex_str(&p[6], uid_len, '-');

  if (sak & 0x20) {
    // ISO-DEP (ISO 14443-4): z.B. Android-Handy im HCE-Modus -> Schluessel pruefen
    ESP_LOGD(TAG, "ISO-DEP Geraet (UID %s, SAK 0x%02X) - starte Challenge", uid.c_str(), (unsigned) sak);
    AuthResult result = this->authenticate_(tg);
    this->cooldown_until_ = millis() + COOLDOWN_PHONE_MS;
    // Revision 2 A/B-Test: HF-Feld nach Handy-Authentifizierung nicht explizit abschalten.
    // InListPassiveTarget startet nach dem Cooldown die naechste Suche.
    if (result == AuthResult::OK) {
      this->key_ok_cb_.call();
    } else if (result == AuthResult::BAD_KEY) {
      this->key_fail_cb_.call();
    } else {
      // Kommunikationsfehler ist KEIN falscher Schluessel.
      // Keine Freigabe und kein on_key_fail; naechster Scan startet eine neue Sitzung.
      ESP_LOGW(TAG, "NFC-Kommunikationsfehler - neuer Versuch nach Cooldown");
    }
    return;
  }

  // Normales Tag (z.B. MIFARE/NTAG): UID melden, aber nicht dauernd wiederholen
  if (uid != this->last_uid_) {
    this->last_uid_ = uid;
    ESP_LOGD(TAG, "Tag erkannt: %s", uid.c_str());
    this->tag_cb_.call(uid);
  }
  this->rf_off_();
}

bool KtbNfc::apdu_(uint8_t tg, const std::vector<uint8_t> &apdu, std::vector<uint8_t> &resp) {
  std::vector<uint8_t> cmd = {CMD_IN_DATA_EXCHANGE, tg};
  cmd.insert(cmd.end(), apdu.begin(), apdu.end());
  std::vector<uint8_t> p;
  uint32_t diag_start = millis();
  if (!this->exec_(cmd, 0x41, p, 400)) {
    ESP_LOGW(TAG, "DIAG APDU INS=0x%02X Transportfehler nach %u ms", (unsigned) (apdu.size() > 1 ? apdu[1] : 0),
             (unsigned) (millis() - diag_start));
    return false;
  }
  ESP_LOGD(TAG, "DIAG APDU INS=0x%02X dauer=%u ms PN532-status=0x%02X antwort=%u B",
           (unsigned) (apdu.size() > 1 ? apdu[1] : 0), (unsigned) (millis() - diag_start),
           (unsigned) (p.empty() ? 0xFF : p[0]), (unsigned) (p.empty() ? 0 : p.size() - 1));
  if (p.empty() || (p[0] & 0x3F) != 0x00) {
    ESP_LOGW(TAG, "InDataExchange Status 0x%02X", (unsigned) (p.empty() ? 0xFF : p[0]));
    return false;
  }
  resp.assign(p.begin() + 1, p.end());
  return true;
}

KtbNfc::AuthResult KtbNfc::authenticate_(uint8_t tg) {
  std::vector<uint8_t> resp;

  if (!this->apdu_(tg, ktb::select_apdu(), resp)) {
    ESP_LOGW(TAG, "SELECT fehlgeschlagen - App installiert / Display an?");
    return AuthResult::COMM_ERROR;
  }
  if (resp.size() != 2 || resp[0] != 0x90 || resp[1] != 0x00) {
    ESP_LOGW(TAG, "SELECT abgelehnt: %s", ktb::hex_str(resp.data(), resp.size(), ' ').c_str());
    return AuthResult::COMM_ERROR;
  }

  uint8_t ch[16];
  for (int i = 0; i < 4; i++) {
    uint32_t r = random_uint32();
    memcpy(ch + i * 4, &r, 4);
  }

  if (!this->apdu_(tg, ktb::challenge_apdu(ch), resp)) {
    ESP_LOGW(TAG, "Challenge fehlgeschlagen");
    return AuthResult::COMM_ERROR;
  }
  if (resp.size() != 34 || resp[32] != 0x90 || resp[33] != 0x00) {
    ESP_LOGW(TAG, "Challenge-Antwort ungueltig: %s", ktb::hex_str(resp.data(), resp.size(), ' ').c_str());
    return AuthResult::COMM_ERROR;
  }

  uint8_t expected[32];
  ktb::expected_mac(this->key_, ch, expected);
  bool ok = ktb::ct_equal(expected, resp.data(), 32);
  if (ok) {
    ESP_LOGI(TAG, "Schluessel OK (HMAC stimmt)");
  } else {
    ESP_LOGW(TAG, "Schluessel FALSCH (HMAC stimmt nicht)");
  }
  return ok ? AuthResult::OK : AuthResult::BAD_KEY;
}

}  // namespace ktb_nfc
}  // namespace esphome
