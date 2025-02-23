# ESPHome NRF24L01 Component

This component enables long-range communication between ESP8266 nodes using NRF24L01+ radio modules in ESPHome.

## Features

- Multi-hub support (up to 6 connections)
- Gateway and Hub modes
- Reliable message delivery with acknowledgment
- Automatic retries and connection monitoring
- Support for ESP-07/ESP-12 modules

## Installation

### Method 1: Using external_components
Add to your ESPHome configuration:
```yaml
external_components:
  - source: github://username/esphome-nrf24l01@main
    components: [ nrf24l01 ]
```

### Method 2: Manual Installation
1. Create directory `custom_components/nrf24l01` in your ESPHome config directory
2. Copy all files from repository's `custom_components/nrf24l01` to this directory
3. Add required dependencies to your project

## Dependencies

See `requirements.txt` for Python dependencies. For ESPHome, you'll need:
```ini
lib_deps =
  RF24@1.4.5
```

## Documentation

See [custom_components/nrf24l01/README.md](custom_components/nrf24l01/README.md) for detailed documentation.

## Examples

Check the `example/` directory for configuration examples:
- `gateway.yaml`: Gateway configuration example
- `hub.yaml`: Hub configuration example

## License

This project is licensed under the MIT License - see the LICENSE file for details.