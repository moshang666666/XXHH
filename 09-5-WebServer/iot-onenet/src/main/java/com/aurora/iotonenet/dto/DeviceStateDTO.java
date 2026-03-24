package com.aurora.iotonenet.dto;

/**
 * 设备状态数据传输对象
 */
public class DeviceStateDTO {
    
    private String deviceId;
    private String deviceName;
    private SensorValue temperature;
    private SensorValue humidity;
    private SensorValue light;      // 光照模拟量
    private SensorValue mq2;        // 烟雾浓度
    private ErrorValue error;       // 错误码：0=安全，1=警告，2=严重
    private LedValue led;
    private Long updatedAt;

    public static class SensorValue {
        private Double value;
        private String unit;

        public SensorValue() {}

        public SensorValue(Double value, String unit) {
            this.value = value;
            this.unit = unit;
        }

        public Double getValue() {
            return value;
        }

        public void setValue(Double value) {
            this.value = value;
        }

        public String getUnit() {
            return unit;
        }

        public void setUnit(String unit) {
            this.unit = unit;
        }
    }

    public static class LedValue {
        private Boolean value;

        public LedValue() {}

        public LedValue(Boolean value) {
            this.value = value;
        }

        public Boolean getValue() {
            return value;
        }

        public void setValue(Boolean value) {
            this.value = value;
        }
    }

    public static class ErrorValue {
        private Integer value;  // 0=安全，1=警告，2=严重

        public ErrorValue() {}

        public ErrorValue(Integer value) {
            this.value = value;
        }

        public Integer getValue() {
            return value;
        }

        public void setValue(Integer value) {
            this.value = value;
        }
    }

    public String getDeviceId() {
        return deviceId;
    }

    public void setDeviceId(String deviceId) {
        this.deviceId = deviceId;
    }

    public String getDeviceName() {
        return deviceName;
    }

    public void setDeviceName(String deviceName) {
        this.deviceName = deviceName;
    }

    public SensorValue getTemperature() {
        return temperature;
    }

    public void setTemperature(SensorValue temperature) {
        this.temperature = temperature;
    }

    public SensorValue getHumidity() {
        return humidity;
    }

    public void setHumidity(SensorValue humidity) {
        this.humidity = humidity;
    }

    public SensorValue getLight() {
        return light;
    }

    public void setLight(SensorValue light) {
        this.light = light;
    }

    public SensorValue getMq2() {
        return mq2;
    }

    public void setMq2(SensorValue mq2) {
        this.mq2 = mq2;
    }

    public ErrorValue getError() {
        return error;
    }

    public void setError(ErrorValue error) {
        this.error = error;
    }

    public LedValue getLed() {
        return led;
    }

    public void setLed(LedValue led) {
        this.led = led;
    }

    public Long getUpdatedAt() {
        return updatedAt;
    }

    public void setUpdatedAt(Long updatedAt) {
        this.updatedAt = updatedAt;
    }
}
