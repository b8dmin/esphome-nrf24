import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID

DEPENDENCIES = ['spi']
AUTO_LOAD = ['spi']

# Configuration constants
CONF_CE_PIN = "ce_pin"
CONF_CSN_PIN = "csn_pin"
CONF_MODE = "mode"
CONF_GATEWAY_ADDRESS = "gateway_address"
CONF_HUBS = "hubs"
CONF_ADDRESS = "address"
CONF_PIPE = "pipe"
CONF_CHECK_INTERVAL = 'check_interval'

# Create namespace for component
nrf24_ns = cg.esphome_ns.namespace('nrf24l01')
NRF24Component = nrf24_ns.class_('NRF24L01Component', cg.Component)

# Mode validation
MODE_OPTIONS = {
    "gateway": 0,
    "hub": 1
}

# Hub configuration schema
HUB_SCHEMA = cv.Schema({
    cv.Required(CONF_PIPE): cv.int_range(min=0, max=5),
    cv.Required(CONF_ADDRESS): cv.string
})

# Main configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(NRF24Component),
    cv.Required(CONF_CE_PIN): pins.gpio_output_pin_schema,
    cv.Required(CONF_CSN_PIN): pins.gpio_output_pin_schema,
    cv.Required(CONF_MODE): cv.enum(MODE_OPTIONS, upper=False),
    cv.Optional(CONF_CHECK_INTERVAL, default='10s'): cv.positive_time_period_seconds,
    cv.Optional(CONF_HUBS, default=[]): cv.ensure_list(HUB_SCHEMA),
    cv.Optional(CONF_GATEWAY_ADDRESS): cv.string_strict
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    ce = await cg.gpio_pin_expression(config[CONF_CE_PIN])
    csn = await cg.gpio_pin_expression(config[CONF_CSN_PIN])
    cg.add(var.set_pins(ce, csn))
    
    cg.add(var.set_mode(config[CONF_MODE]))
    
    cg.add(var.set_check_interval(config[CONF_CHECK_INTERVAL]))
    
    # Configure hubs for gateway mode
    if CONF_HUBS in config:
        for hub in config[CONF_HUBS]:
            pipe = hub[CONF_PIPE]
            address = hub[CONF_ADDRESS]
            cg.add(var.add_hub(pipe, address))
    
    # Configure gateway address for hub mode
    if CONF_GATEWAY_ADDRESS in config:
        cg.add(var.set_gateway_address(config[CONF_GATEWAY_ADDRESS])) 