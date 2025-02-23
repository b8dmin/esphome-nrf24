# NRF24L01 Component for ESPHome

This component provides support for NRF24L01+ radio modules in ESPHome, enabling long-range communication between multiple ESP nodes.

## Features

- Multi-hub support (up to 6 connections)
- Reliable message delivery with acknowledgment
- Automatic retries on failed transmissions
- Connection monitoring
- Support for ESP-07/ESP-12 modules
- Gateway and Hub modes support

## Network Architecture

The component supports two operation modes:
1. **Gateway Mode**: Connected to Home Assistant via WiFi, communicates with remote hubs via NRF24
2. **Hub Mode**: Communicates with gateway via NRF24, provides local WiFi network for sensors

### Gateway Features
- Connects to main WiFi network
- Supports up to 5 remote hubs
- Reports hub status to Home Assistant
- Manages message routing

### Hub Features
- Creates local WiFi network for sensors
- Communicates with gateway via NRF24
- Manages local sensor connections
- Forwards sensor data to gateway

## Dependencies

- ESPHome 2023.12.0 or newer
- RF24 library
- SPI support

## Hardware Setup

### NRF24L01+ Pinout

```
NRF24L01+ Module Pin Layout:
1. GND    - Ground
2. VCC    - 3.3V
3. CE     - Chip Enable
4. CSN    - Chip Select
5. SCK    - SPI Clock
6. MOSI   - SPI MOSI
7. MISO   - SPI MISO
8. IRQ    - Interrupt (not used)
```

### Wiring for ESP-07/ESP-12

```
ESP-07/12  -> NRF24L01+
3.3V       -> VCC
GND        -> GND
GPIO4      -> CE
GPIO5      -> CSN
GPIO14     -> SCK
GPIO13     -> MOSI
GPIO12     -> MISO
```

⚠️ ESP-01 has too few GPIO pins for NRF24L01+ direct connection. Not recommended for this project.

#### ESP-07
Available GPIO:
- GPIO0  - Boot mode selection (needs pull-up for normal operation)
- GPIO2  - Boot mode selection (needs pull-up for normal operation)
- GPIO4  - Safe to use
- GPIO5  - Safe to use
- GPIO12 - Safe to use (MISO)
- GPIO13 - Safe to use (MOSI)
- GPIO14 - Safe to use (SCK)
- GPIO15 - Boot mode selection (needs pull-down for normal operation)
- GPIO16 - Wake up from deep sleep (has limitations)

Recommended ESP-07 wiring for NRF24L01+:

```
ESP-07     -> NRF24L01+
3.3V       -> VCC
GND        -> GND
GPIO4      -> CE
GPIO5      -> CSN
GPIO14     -> SCK
GPIO13     -> MOSI
GPIO12     -> MISO
```

⚠️ Important GPIO Boot Mode Notes:
- GPIO0: Must be HIGH during boot (pull-up resistor)
- GPIO2: Must be HIGH during boot (pull-up resistor)
- GPIO15: Must be LOW during boot (pull-down resistor)

For programming:
- GPIO0 needs to be pulled LOW during flashing
- TX (GPIO1) and RX (GPIO3) are needed for serial communication during programming

### Power Supply Considerations
- ESP-07 has better power regulation than ESP-01
- Still recommended to use additional capacitor (10µF) between VCC and GND of NRF24L01+
- Consider using separate 3.3V regulator for NRF24L01+ if experiencing stability issues

## Configuration

### Gateway Mode Configuration

```yaml
# Gateway configuration (connects to HA)
esphome:
  name: nrf24_gateway

wifi:
  ssid: "Your_Main_WiFi"
  password: "Your_Password"

# Configure NRF24L01
nrf24l01:
  ce_pin: GPIO4
  csn_pin: GPIO5
  mode: gateway  # Explicitly set gateway mode
  hubs:
    - pipe: 0
      address: "HUB01"
```

### Hub Mode Configuration

```yaml
# Hub configuration (creates local network for sensors)
esphome:
  name: nrf24_hub

wifi:
  ap:
    ssid: "Sensors_Network"
    password: "AP_Password"

# Configure NRF24L01
nrf24l01:
  ce_pin: GPIO4
  csn_pin: GPIO5
  mode: hub  # Explicitly set hub mode
  gateway_address: "HUB01"  # Must match gateway configuration
```

## Component Features

### Message Handling
- Automatic message acknowledgment
- Message retry system (up to 3 retries)
- Message queuing
- Unique message IDs

### Connection Monitoring
- Hub connection status tracking
- Connection timeout detection (60 seconds)
- Last seen timestamp for each hub

### Error Handling
- Hardware initialization verification
- Invalid hub detection
- Transmission failure handling
- Comprehensive error logging

## Troubleshooting

Common issues and solutions:

1. **No Communication**
   - Check power supply stability
   - Verify wiring connections
   - Ensure matching addresses between gateway and hubs
   - Check SPI configuration

2. **Poor Range**
   - Use external antenna version
   - Minimize obstacles between nodes
   - Check power supply quality
   - Position antennas vertically

3. **Intermittent Connection**
   - Add capacitor to power supply
   - Reduce interference from other devices
   - Check for proper grounding
   - Verify power supply stability

## Performance Optimization

1. **Range Optimization**
   - Use PA_MAX power setting
   - Lower data rate (250KBPS)
   - Proper antenna positioning
   - Minimize interference

2. **Reliability**
   - Implement proper error handling
   - Use acknowledgment system
   - Configure appropriate retry delays
   - Monitor connection status

## Technical Specifications

- Maximum nodes: 1 gateway + 5 hubs
- Message size: 24 bytes payload
- Retry attempts: 3
- Retry delay: 100ms
- ACK timeout: 50ms
- Hub timeout: 60 seconds
- Radio settings:
  - Power: PA_MAX
  - Data rate: 250KBPS
  - Channel: 76

## Contributing

Feel free to contribute to this component by:
1. Reporting issues
2. Suggesting improvements
3. Creating pull requests

## License

This project is licensed under the MIT License - see the LICENSE file for details.

### Additional Files Required

For proper installation, ensure you have:
1. RF24 library installed
2. SPI enabled in your ESPHome configuration

### Example platformio.ini dependencies:
```ini
lib_deps =
  RF24@1.4.5
```

### Complete ESPHome Example:
```yaml
# Basic configuration
esphome:
  name: nrf24_gateway
  platform: ESP8266

# Enable SPI
spi:
  clk_pin: GPIO14
  mosi_pin: GPIO13
  miso_pin: GPIO12

# Enable logging
logger:
  level: DEBUG  # Recommended for initial setup

# Configure NRF24L01
nrf24l01:
  ce_pin: GPIO4
  csn_pin: GPIO5
  hubs:
    - pipe: 0
      address: "HUB01"
```

CONF_MODE = "mode"
CONF_GATEWAY_ADDRESS = "gateway_address" 