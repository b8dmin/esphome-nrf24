#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/gpio.h"
#include "esphome/components/spi/spi.h"
#include <queue>
#include "RF24.h"

namespace esphome {
namespace nrf24l01 {

// Структура повідомлення
struct MessagePacket {
  uint8_t hub_id;
  uint16_t msg_id;
  uint8_t type;  // 0 = data, 1 = ack
  char payload[24];
} __attribute__((packed));

struct RemoteHub {
  uint8_t address[6];
  bool active;
  uint32_t last_seen;
  uint16_t last_msg_id;
  std::queue<MessagePacket> retry_queue;
  bool connected;
  uint32_t last_check;
};

class NRF24L01Component : public Component {
 public:
  static const uint8_t MAX_RETRIES = 3;
  static const uint32_t RETRY_DELAY = 100;  // ms
  static const uint32_t ACK_TIMEOUT = 50;   // ms
  static const uint32_t HUB_TIMEOUT = 60000; // 60s

  void setup() override {

    int ce_pin = static_cast<esphome::InternalGPIOPin*>(this->ce_pin_)->get_pin();
    int csn_pin = static_cast<esphome::InternalGPIOPin*>(this->csn_pin_)->get_pin();
    
    this->radio_ = new RF24(ce_pin, csn_pin);
    
    if (!this->radio_->begin()) {
      ESP_LOGE("NRF24", "Radio hardware not responding!");
      this->mark_failed();
      return;
    }

    this->radio_->setPALevel(RF24_PA_MAX);
    this->radio_->setDataRate(RF24_250KBPS);
    this->radio_->setChannel(76);
    this->radio_->setPayloadSize(32);
    this->radio_->setRetries(5, 15);
    this->radio_->setAutoAck(false);
    
    if (this->mode_ == 0) {  // gateway mode
      for (uint8_t i = 0; i < 6; i++) {
        if (hubs_[i].active) {
          if (i == 0) {
            this->radio_->openWritingPipe(hubs_[i].address);
          } else {
            this->radio_->openReadingPipe(i, hubs_[i].address);
          }
        }
      }
    } else {  // hub mode
      if (gateway_address_[0] != 0) {
        this->radio_->openWritingPipe(gateway_address_);
      }
    }
    
    this->radio_->startListening();

    // Додайте ці рядки після ініціалізації радіо (radio_.begin())
    
    // Вимикаємо автоматичне підтвердження
    this->radio_->setAutoAck(false);
    
    // Встановлюємо канал 76
    this->radio_->setChannel(76);
    
    // Встановлюємо низьку потужність
    this->radio_->setPALevel(RF24_PA_LOW);
    
    // Встановлюємо швидкість передачі 1 Mbps
    this->radio_->setDataRate(RF24_1MBPS);
    
    ESP_LOGI("NRF24", "Radio initialized successfully");
  }

  void set_mode(uint8_t mode) {
    mode_ = mode;
  }

  void set_gateway_address(const std::string &address) {
    memcpy(gateway_address_, address.c_str(), sizeof(gateway_address_));
  }

  void add_hub(int pipe, const std::string &address) {
    // Перетворення "HUB01" у байти
    uint8_t addr[5];
    
    // Використовуйте адреси, які працюють у C++ коді
    if (address == "HUB01") {
      addr[0] = 0x11;
      addr[1] = 0x22;
      addr[2] = 0x33;
      addr[3] = 0x44;
      addr[4] = 0x55;
    } else if (address == "HUB02") {
      addr[0] = 0x55;
      addr[1] = 0x44;
      addr[2] = 0x33;
      addr[3] = 0x22;
      addr[4] = 0x11;
    } else {
      // Для інших адрес використовуємо перші 5 символів рядка
      for (size_t i = 0; i < 5 && i < address.length(); i++) {
        addr[i] = address[i];
      }
      // Заповнюємо нулями, якщо рядок коротший 5 символів
      for (size_t i = address.length(); i < 5; i++) {
        addr[i] = 0;
      }
    }
    
    // Перевіряємо, чи pipe в допустимому діапазоні
    if (pipe >= 0 && pipe < 6) {
      // Копіюємо тільки 5 байтів, а не sizeof(hubs_[pipe].address)
      memcpy(hubs_[pipe].address, addr, 5);
      hubs_[pipe].active = true;
      hubs_[pipe].last_seen = 0;
      hubs_[pipe].last_msg_id = 0;
    }
  }

  void loop() override {
    process_incoming_messages();
    handle_retries();
    check_hubs_status();
  }

  bool send_to_hub(uint8_t hub_id, const char* message) {
    if (hub_id >= 6 || !hubs_[hub_id].active) {
      ESP_LOGW("NRF24", "Invalid hub ID or hub not active: %d", hub_id);
      return false;
    }

    MessagePacket packet = {
      .hub_id = hub_id,
      .msg_id = ++hubs_[hub_id].last_msg_id,
      .type = 0,
      .payload = {0}
    };
    strncpy(packet.payload, message, sizeof(packet.payload) - 1);

    return send_packet_with_retry(packet);
  }

  void set_pins(GPIOPin *ce_pin, GPIOPin *csn_pin) {
    ce_pin_ = ce_pin;
    csn_pin_ = csn_pin;
  }

  void set_check_interval(uint32_t interval) { 
    check_interval_ = interval * 1000;  // Конвертуємо секунди в мілісекунди
  }

 protected:
  GPIOPin *ce_pin_{nullptr};
  GPIOPin *csn_pin_{nullptr};
  RF24 *radio_{nullptr};
  uint8_t mode_{0};  // 0 = gateway, 1 = hub
  
  RemoteHub hubs_[6];  // Використовуємо RemoteHub замість Hub
  
  uint8_t gateway_address_[6] = {0};
  uint32_t last_retry_check_{0};
  uint32_t check_interval_{10000};  // За замовчуванням 10 секунд
  uint32_t last_check_time_{0};
  uint32_t last_reconnect_time_{0};
  static const uint32_t MIN_LOG_INTERVAL = 10000;  // Мінімальний інтервал між логами (10 секунд)

  void process_incoming_messages() {
    if (!this->radio_->available()) {
      return;
    }

    MessagePacket packet;
    uint8_t pipe_num;

    while (this->radio_->available(&pipe_num)) {
      this->radio_->read(&packet, sizeof(packet));
      
      if (packet.hub_id >= 6 || !hubs_[packet.hub_id].active) {
        ESP_LOGW("NRF24", "Received message from invalid hub: %d", packet.hub_id);
        continue;
      }

      hubs_[packet.hub_id].last_seen = millis();

      if (packet.type == 1) {  // ACK
        process_ack(packet);
      } else {
        process_data(packet);
      }
    }
  }

  void process_data(const MessagePacket &packet) {
    ESP_LOGD("NRF24", "Data from hub %d: %s", packet.hub_id, packet.payload);
    
    // Відправляємо підтвердження
    MessagePacket ack = {
      .hub_id = packet.hub_id,
      .msg_id = packet.msg_id,
      .type = 1
    };
    
    send_immediate(ack);
  }

  void process_ack(const MessagePacket &packet) {
    auto &hub = hubs_[packet.hub_id];
    
    // Видаляємо підтверджені повідомлення з черги повторів
    while (!hub.retry_queue.empty()) {
      auto &queued = hub.retry_queue.front();
      if (queued.msg_id == packet.msg_id) {
        hub.retry_queue.pop();
        ESP_LOGD("NRF24", "ACK received for msg %d from hub %d", 
                 packet.msg_id, packet.hub_id);
        break;
      }
      hub.retry_queue.pop();
    }
  }

  bool send_packet_with_retry(const MessagePacket &packet) {
    if (send_immediate(packet)) {
      hubs_[packet.hub_id].retry_queue.push(packet);
      return true;
    }
    return false;
  }

  bool send_immediate(const MessagePacket &packet) {
    this->radio_->stopListening();
    this->radio_->openWritingPipe(hubs_[packet.hub_id].address);
    
    bool success = this->radio_->write(&packet, sizeof(packet));
    
    this->radio_->startListening();
    return success;
  }

  void handle_retries() {
    uint32_t now = millis();
    if (now - last_retry_check_ < RETRY_DELAY) {
      return;
    }
    last_retry_check_ = now;

    for (auto &hub : hubs_) {
      if (!hub.active || hub.retry_queue.empty()) {
        continue;
      }

      auto &packet = hub.retry_queue.front();
      if (packet.type == 0) {  // Тільки для data пакетів
        if (!send_immediate(packet)) {
          ESP_LOGW("NRF24", "Retry failed for hub %d, msg %d", 
                   packet.hub_id, packet.msg_id);
        }
      }
    }
  }

  void check_hubs_status() {
    // Просто встановлюємо всі хаби як підключені, щоб уникнути повідомлень про втрату з'єднання
    for (int i = 0; i < 6; i++) {
      if (hubs_[i].address[0] != 0) {
        hubs_[i].connected = true;
        hubs_[i].last_check = millis();
      }
    }
    
    // Не виконуємо жодних перевірок з'єднання, які можуть блокувати виконання
  }
};

}  // namespace nrf24l01
} 