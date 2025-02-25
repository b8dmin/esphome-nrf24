#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/gpio.h"
#include "esphome/components/spi/spi.h"
#include <queue>
#include <vector>
#include <string>
#include "RF24.h"

namespace esphome {
namespace nrf24l01 {

// Message types
enum MessageType {
  DATA_MESSAGE = 0,
  ACK_MESSAGE = 1,
  SENSOR_DATA = 2,
  COMMAND = 3,
  STATUS = 4
};

// Message structure
struct MessagePacket {
  uint8_t hub_id;
  uint16_t msg_id;
  uint8_t type;  // MessageType
  char payload[24];
} __attribute__((packed));

// Hub information structure
struct RemoteHub {
  uint8_t address[6];
  bool active;
  uint32_t last_seen;
  uint16_t last_msg_id;
  std::queue<MessagePacket> retry_queue;
  uint8_t retry_count[256]; // Track retries for each message
};

class NRF24L01Component : public Component {
 public:
  static const uint8_t MAX_RETRIES = 3;
  static const uint32_t RETRY_DELAY = 100;  // ms
  static const uint32_t ACK_TIMEOUT = 50;   // ms
  static const uint32_t HUB_TIMEOUT = 60000; // 60s

  void setup() override {
    ESP_LOGI("NRF24", "Initializing NRF24L01 in %s mode", mode_ == 0 ? "gateway" : "hub");

    int ce_pin = static_cast<esphome::InternalGPIOPin*>(this->ce_pin_)->get_pin();
    int csn_pin = static_cast<esphome::InternalGPIOPin*>(this->csn_pin_)->get_pin();
    
    this->radio_ = new RF24(ce_pin, csn_pin);
    
    if (!this->radio_->begin()) {
      ESP_LOGE("NRF24", "Radio hardware not responding!");
      this->mark_failed();
      return;
    }

    // Configure radio with optimal settings for range and reliability
    this->radio_->setPALevel(RF24_PA_MAX);
    this->radio_->setDataRate(RF24_250KBPS);
    this->radio_->setChannel(76);
    this->radio_->setPayloadSize(sizeof(MessagePacket));
    this->radio_->setRetries(5, 15);
    this->radio_->setAutoAck(false); // We'll handle ACKs manually
    
    if (this->mode_ == 0) {  // Gateway mode
      ESP_LOGI("NRF24", "Configuring gateway with %d hubs", count_active_hubs());
      for (uint8_t i = 0; i < 6; i++) {
        if (hubs_[i].active) {
          ESP_LOGI("NRF24", "Setting up hub %d with address: %02X:%02X:%02X:%02X:%02X", 
                  i, hubs_[i].address[0], hubs_[i].address[1], hubs_[i].address[2], 
                  hubs_[i].address[3], hubs_[i].address[4]);
          
          this->radio_->openReadingPipe(i, hubs_[i].address);
        }
      }
    } else {  // Hub mode
      if (gateway_address_[0] != 0) {
        ESP_LOGI("NRF24", "Configuring hub with gateway address: %02X:%02X:%02X:%02X:%02X", 
                gateway_address_[0], gateway_address_[1], gateway_address_[2], 
                gateway_address_[3], gateway_address_[4]);
        
        this->radio_->openWritingPipe(gateway_address_);
        this->radio_->openReadingPipe(1, gateway_address_); // Also listen to gateway
      } else {
        ESP_LOGE("NRF24", "No gateway address configured for hub mode!");
        this->mark_failed();
        return;
      }
    }
    
    this->radio_->startListening();
    ESP_LOGI("NRF24", "Radio initialized successfully");
  }

  void set_mode(uint8_t mode) {
    mode_ = mode;
  }

  void set_gateway_address(const std::string &address) {
    convert_address_to_bytes(address, gateway_address_);
  }

  void add_hub(int pipe, const std::string &address) {
    if (pipe >= 0 && pipe < 6) {
      convert_address_to_bytes(address, hubs_[pipe].address);
      hubs_[pipe].active = true;
      hubs_[pipe].last_seen = 0;
      hubs_[pipe].last_msg_id = 0;
      
      // Initialize retry counts
      for (int i = 0; i < 256; i++) {
        hubs_[pipe].retry_count[i] = 0;
      }
    }
  }

  void loop() override {
    process_incoming_messages();
    handle_retries();
    check_hubs_status();
    
    // Send periodic status updates in hub mode
    if (mode_ == 1) { // Hub mode
      uint32_t now = millis();
      if (now - last_status_update_ > STATUS_UPDATE_INTERVAL) {
        send_status_update();
        last_status_update_ = now;
      }
    }
  }

  // Send data to a specific hub (gateway mode)
  bool send_to_hub(uint8_t hub_id, const char* message) {
    if (mode_ != 0) {
      ESP_LOGW("NRF24", "send_to_hub can only be used in gateway mode");
      return false;
    }
    
    if (hub_id >= 6 || !hubs_[hub_id].active) {
      ESP_LOGW("NRF24", "Invalid hub ID or hub not active: %d", hub_id);
      return false;
    }

    MessagePacket packet = {
      .hub_id = hub_id,
      .msg_id = ++hubs_[hub_id].last_msg_id,
      .type = COMMAND,
      .payload = {0}
    };
    strncpy(packet.payload, message, sizeof(packet.payload) - 1);

    return send_packet_with_retry(packet);
  }

  // Send data to gateway (hub mode)
  bool send_to_gateway(const char* message, uint8_t type = SENSOR_DATA) {
    if (mode_ != 1) {
      ESP_LOGW("NRF24", "send_to_gateway can only be used in hub mode");
      return false;
    }
    
    if (gateway_address_[0] == 0) {
      ESP_LOGW("NRF24", "No gateway address configured");
      return false;
    }

    MessagePacket packet = {
      .hub_id = 0, // Will be set by gateway
      .msg_id = ++last_msg_id_,
      .type = type,
      .payload = {0}
    };
    strncpy(packet.payload, message, sizeof(packet.payload) - 1);

    return send_gateway_packet(packet);
  }

  void set_pins(GPIOPin *ce_pin, GPIOPin *csn_pin) {
    ce_pin_ = ce_pin;
    csn_pin_ = csn_pin;
  }

  void set_check_interval(uint32_t interval) { 
    check_interval_ = interval * 1000;  // Convert seconds to milliseconds
  }

  float get_hub_status(uint8_t hub_id) {
    if (hub_id >= 6 || !hubs_[hub_id].active) {
      return 0.0f;
    }
    
    uint32_t now = millis();
    if (hubs_[hub_id].last_seen > 0 && now - hubs_[hub_id].last_seen < HUB_TIMEOUT) {
      return 1.0f;
    }
    return 0.0f;
  }

  std::string get_last_message(uint8_t hub_id) {
    if (hub_id >= 6 || !hubs_[hub_id].active) {
      return "";
    }
    
    return last_messages_[hub_id];
  }

 protected:
  GPIOPin *ce_pin_{nullptr};
  GPIOPin *csn_pin_{nullptr};
  RF24 *radio_{nullptr};
  uint8_t mode_{0};  // 0 = gateway, 1 = hub
  
  RemoteHub hubs_[6];
  uint8_t gateway_address_[6] = {0};
  
  uint32_t last_retry_check_{0};
  uint32_t check_interval_{10000};  // Default 10 seconds
  uint32_t last_check_time_{0};
  uint32_t last_reconnect_time_{0};
  uint32_t last_status_update_{0};
  uint16_t last_msg_id_{0};  // For hub mode
  
  std::string last_messages_[6];  // Store last message from each hub
  
  static const uint32_t MIN_LOG_INTERVAL = 10000;  // Minimum interval between logs (10 seconds)
  static const uint32_t STATUS_UPDATE_INTERVAL = 5000;  // Status update every 5 seconds

  void process_incoming_messages() {
    if (!this->radio_->available()) {
      return;
    }

    MessagePacket packet;
    uint8_t pipe_num;

    while (this->radio_->available(&pipe_num)) {
      this->radio_->read(&packet, sizeof(packet));
      
      if (mode_ == 0) {  // Gateway mode
        process_gateway_message(packet, pipe_num);
      } else {  // Hub mode
        process_hub_message(packet);
      }
    }
  }

  void process_gateway_message(const MessagePacket &packet, uint8_t pipe_num) {
    // In gateway mode, we receive messages from hubs
    if (pipe_num >= 6 || !hubs_[pipe_num].active) {
      ESP_LOGW("NRF24", "Received message from invalid pipe: %d", pipe_num);
      return;
    }

    // Update hub status
    hubs_[pipe_num].last_seen = millis();

    // Process by message type
    switch (packet.type) {
      case ACK_MESSAGE:
        process_ack(packet, pipe_num);
        break;
      
      case SENSOR_DATA:
        ESP_LOGD("NRF24", "Sensor data from hub %d: %s", pipe_num, packet.payload);
        last_messages_[pipe_num] = packet.payload;
        send_ack(pipe_num, packet.msg_id);
        break;
        
      case STATUS:
        ESP_LOGD("NRF24", "Status from hub %d: %s", pipe_num, packet.payload);
        send_ack(pipe_num, packet.msg_id);
        break;
        
      default:
        ESP_LOGW("NRF24", "Unknown message type %d from hub %d", packet.type, pipe_num);
        break;
    }
  }

  void process_hub_message(const MessagePacket &packet) {
    // In hub mode, we receive messages from gateway
    
    // Process by message type
    switch (packet.type) {
      case ACK_MESSAGE:
        // Process ACK for our sent messages
        if (packet.msg_id == last_msg_id_) {
          ESP_LOGD("NRF24", "ACK received from gateway for msg %d", packet.msg_id);
        }
        break;
      
      case COMMAND:
        ESP_LOGD("NRF24", "Command from gateway: %s", packet.payload);
        // Process command (implement command handling)
        send_ack_to_gateway(packet.msg_id);
        break;
        
      default:
        ESP_LOGW("NRF24", "Unknown message type %d from gateway", packet.type);
        break;
    }
  }

  void process_ack(const MessagePacket &packet, uint8_t hub_id) {
    auto &hub = hubs_[hub_id];
    
    // Remove acknowledged messages from retry queue
    while (!hub.retry_queue.empty()) {
      auto &queued = hub.retry_queue.front();
      if (queued.msg_id == packet.msg_id) {
        hub.retry_queue.pop();
        ESP_LOGD("NRF24", "ACK received for msg %d from hub %d", 
                 packet.msg_id, hub_id);
        break;
      }
      hub.retry_queue.pop();
    }
  }

  bool send_packet_with_retry(const MessagePacket &packet) {
    uint8_t hub_id = packet.hub_id;
    
    if (send_immediate(packet, hubs_[hub_id].address)) {
      hubs_[hub_id].retry_queue.push(packet);
      hubs_[hub_id].retry_count[packet.msg_id % 256] = 0;
      return true;
    }
    return false;
  }

  bool send_gateway_packet(const MessagePacket &packet) {
    this->radio_->stopListening();
    bool success = this->radio_->write(&packet, sizeof(packet));
    this->radio_->startListening();
    
    if (!success) {
      ESP_LOGW("NRF24", "Failed to send message to gateway");
    }
    
    return success;
  }

  bool send_immediate(const MessagePacket &packet, uint8_t* address) {
    this->radio_->stopListening();
    this->radio_->openWritingPipe(address);
    
    bool success = this->radio_->write(&packet, sizeof(packet));
    
    this->radio_->startListening();
    return success;
  }

  void send_ack(uint8_t hub_id, uint16_t msg_id) {
    MessagePacket ack = {
      .hub_id = hub_id,
      .msg_id = msg_id,
      .type = ACK_MESSAGE,
      .payload = {0}
    };
    
    send_immediate(ack, hubs_[hub_id].address);
  }

  void send_ack_to_gateway(uint16_t msg_id) {
    MessagePacket ack = {
      .hub_id = 0,
      .msg_id = msg_id,
      .type = ACK_MESSAGE,
      .payload = {0}
    };
    
    send_gateway_packet(ack);
  }

  void handle_retries() {
    uint32_t now = millis();
    if (now - last_retry_check_ < RETRY_DELAY) {
      return;
    }
    last_retry_check_ = now;

    for (uint8_t i = 0; i < 6; i++) {
      auto &hub = hubs_[i];
      if (!hub.active || hub.retry_queue.empty()) {
        continue;
      }

      auto &packet = hub.retry_queue.front();
      uint8_t retry_idx = packet.msg_id % 256;
      
      if (hub.retry_count[retry_idx] >= MAX_RETRIES) {
        // Max retries reached, drop the message
        ESP_LOGW("NRF24", "Max retries reached for hub %d, msg %d", 
                 i, packet.msg_id);
        hub.retry_queue.pop();
        continue;
      }
      
      if (send_immediate(packet, hub.address)) {
        hub.retry_count[retry_idx]++;
        ESP_LOGD("NRF24", "Retry %d for hub %d, msg %d", 
                 hub.retry_count[retry_idx], i, packet.msg_id);
      } else {
        ESP_LOGW("NRF24", "Retry failed for hub %d, msg %d", 
                 i, packet.msg_id);
        hub.retry_count[retry_idx]++;
      }
    }
  }

  void check_hubs_status() {
    uint32_t now = millis();
    
    // Check if interval has passed
    if (now - last_check_time_ < check_interval_) {
      return;
    }
    last_check_time_ = now;

    if (mode_ == 0) {  // Only in gateway mode
      bool need_reconnect = false;
      for (uint8_t i = 0; i < 6; i++) {
        if (hubs_[i].active) {
          if (now - hubs_[i].last_seen > HUB_TIMEOUT) {
            ESP_LOGW("NRF24", "Hub %d connection lost", i);
            need_reconnect = true;
          } else {
            ESP_LOGD("NRF24", "Hub %d connected, last seen %d ms ago", 
                     i, now - hubs_[i].last_seen);
          }
        }
      }

      // Try to reconnect only after interval
      if (need_reconnect && (now - last_reconnect_time_ >= check_interval_)) {
        last_reconnect_time_ = now;
        ESP_LOGI("NRF24", "Attempting to reconnect hubs...");
        
        // Restart radio
        this->radio_->powerDown();
        delay(100);
        this->radio_->powerUp();
        
        // Reconfigure pipes for hubs
        for (uint8_t i = 0; i < 6; i++) {
          if (hubs_[i].active) {
            this->radio_->openReadingPipe(i, hubs_[i].address);
          }
        }
        
        this->radio_->startListening();
      }
    }
  }

  void send_status_update() {
    // In hub mode, send periodic status updates to gateway
    char status[24];
    snprintf(status, sizeof(status), "HUB_ALIVE:%u", millis());
    send_to_gateway(status, STATUS);
  }

  void convert_address_to_bytes(const std::string &address_str, uint8_t* address_bytes) {
    // Convert string address to byte array
    if (address_str == "HUB01") {
      address_bytes[0] = 0x11;
      address_bytes[1] = 0x22;
      address_bytes[2] = 0x33;
      address_bytes[3] = 0x44;
      address_bytes[4] = 0x55;
    } else if (address_str == "HUB02") {
      address_bytes[0] = 0x55;
      address_bytes[1] = 0x44;
      address_bytes[2] = 0x33;
      address_bytes[3] = 0x22;
      address_bytes[4] = 0x11;
    } else if (address_str == "HUB03") {
      address_bytes[0] = 0xAA;
      address_bytes[1] = 0xBB;
      address_bytes[2] = 0xCC;
      address_bytes[3] = 0xDD;
      address_bytes[4] = 0xEE;
    } else if (address_str == "HUB04") {
      address_bytes[0] = 0xEE;
      address_bytes[1] = 0xDD;
      address_bytes[2] = 0xCC;
      address_bytes[3] = 0xBB;
      address_bytes[4] = 0xAA;
    } else if (address_str == "HUB05") {
      address_bytes[0] = 0x12;
      address_bytes[1] = 0x34;
      address_bytes[2] = 0x56;
      address_bytes[3] = 0x78;
      address_bytes[4] = 0x9A;
    } else {
      // For other addresses, use first 5 characters of string
      for (size_t i = 0; i < 5 && i < address_str.length(); i++) {
        address_bytes[i] = address_str[i];
      }
      // Fill with zeros if string is shorter than 5 characters
      for (size_t i = address_str.length(); i < 5; i++) {
        address_bytes[i] = 0;
      }
    }
  }

  int count_active_hubs() {
    int count = 0;
    for (int i = 0; i < 6; i++) {
      if (hubs_[i].active) count++;
    }
    return count;
  }
};

}  // namespace nrf24l01
}  // namespace esphome 