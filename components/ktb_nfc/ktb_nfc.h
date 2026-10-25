#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"

#include <string>
#include <vector>

#include "ktb_proto.h"

namespace esphome {
namespace ktb_nfc {

class KtbNfc : public PollingComponent,
               public spi::SPIDevice<spi::BIT_ORDER_LSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                     spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_key_hex(const std::string &hex);

  void add_on_tag_callback(std::function<void(const std::string &)> &&cb) { this->tag_cb_.add(std::move(cb)); }
  void add_on_key_ok_callback(std::function<void()> &&cb) { this->key_ok_cb_.add(std::move(cb)); }
  void add_on_key_fail_callback(std::function<void()> &&cb) { this->key_fail_cb_.add(std::move(cb)); }

 protected:
  enum class State : uint8_t { IDLE, WAIT_LIST };

  // SPI-Ebene
  void write_frame_(const std::vector<uint8_t> &frame, uint32_t wake_ms = 0);
  void read_raw_(uint8_t *buf, size_t len);
  bool is_ready_();
  bool wait_ready_(uint32_t timeout_ms);

  // PN532-Befehle
  bool send_command_(const std::vector<uint8_t> &cmd);
  bool recv_response_(uint8_t resp_code, std::vector<uint8_t> &payload);
  bool exec_(const std::vector<uint8_t> &cmd, uint8_t resp_code, std::vector<uint8_t> &payload,
             uint32_t timeout_ms = 100);
  void abort_command_();
  void rf_off_();

  // Logik
  bool init_pn532_();
  void handle_list_response_(const std::vector<uint8_t> &p);
  bool apdu_(uint8_t tg, const std::vector<uint8_t> &apdu, std::vector<uint8_t> &resp);
  enum class AuthResult : uint8_t { OK, BAD_KEY, COMM_ERROR };
  AuthResult authenticate_(uint8_t tg);

  uint8_t key_[32]{};
  bool key_valid_{false};
  bool pn_ready_{false};
  uint32_t next_init_{0};
  State state_{State::IDLE};
  uint32_t state_ts_{0};
  uint32_t cooldown_until_{0};
  std::string last_uid_;

  CallbackManager<void(const std::string &)> tag_cb_;
  CallbackManager<void()> key_ok_cb_;
  CallbackManager<void()> key_fail_cb_;
};

class TagTrigger : public Trigger<std::string> {
 public:
  explicit TagTrigger(KtbNfc *parent) {
    parent->add_on_tag_callback([this](const std::string &uid) { this->trigger(uid); });
  }
};

class KeyOkTrigger : public Trigger<> {
 public:
  explicit KeyOkTrigger(KtbNfc *parent) {
    parent->add_on_key_ok_callback([this]() { this->trigger(); });
  }
};

class KeyFailTrigger : public Trigger<> {
 public:
  explicit KeyFailTrigger(KtbNfc *parent) {
    parent->add_on_key_fail_callback([this]() { this->trigger(); });
  }
};

}  // namespace ktb_nfc
}  // namespace esphome
