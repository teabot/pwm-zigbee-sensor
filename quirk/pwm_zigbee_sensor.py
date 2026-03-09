"""ZHA quirk for Teabot PWM Zigbee Sensor (ESP32-H2)."""

from zigpy.profiles import zha
from zigpy.quirks import CustomDevice
from zigpy.zcl.clusters.general import Basic, Identify
from zigpy.zcl.clusters.measurement import RelativeHumidity

from zhaquirks import CustomCluster
from zhaquirks.const import (
    DEVICE_TYPE,
    ENDPOINTS,
    INPUT_CLUSTERS,
    MANUFACTURER,
    MODEL,
    OUTPUT_CLUSTERS,
    PROFILE_ID,
)

_DEVICE_TYPE = 0x0307


class PwmDutyCycleCluster(CustomCluster, RelativeHumidity):
    """Relative Humidity cluster repurposed for PWM duty cycle (0–100 %).

    measured_value = duty_percent * 100; ZHA divides by 100 to display.
    Overrides ep_attribute so entity IDs reflect 'duty_cycle' not 'humidity'.
    """

    ep_attribute = "duty_cycle"


class PWMZigbeeSensor(CustomDevice):
    """Two-channel PWM duty cycle sensor (Teabot / ESP32-H2)."""

    signature = {
        MANUFACTURER: "Teabot",
        MODEL: "PWMSensor",
        ENDPOINTS: {
            1: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [
                    Basic.cluster_id,
                    Identify.cluster_id,
                    RelativeHumidity.cluster_id,  # 0x0405
                ],
                OUTPUT_CLUSTERS: [],
            },
            2: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [
                    Basic.cluster_id,
                    Identify.cluster_id,
                    RelativeHumidity.cluster_id,
                ],
                OUTPUT_CLUSTERS: [],
            },
        },
    }

    replacement = {
        ENDPOINTS: {
            1: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [Basic, Identify, PwmDutyCycleCluster],
                OUTPUT_CLUSTERS: [],
            },
            2: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [Basic, Identify, PwmDutyCycleCluster],
                OUTPUT_CLUSTERS: [],
            },
        },
    }
