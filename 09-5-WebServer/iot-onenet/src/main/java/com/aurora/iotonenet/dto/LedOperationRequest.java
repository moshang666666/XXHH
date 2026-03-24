package com.aurora.iotonenet.dto;

/**
 * LED操作请求DTO
 */
public class LedOperationRequest {
    
    private String deviceId;
    private String deviceName;
    private Boolean led;

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

    public Boolean getLed() {
        return led;
    }

    public void setLed(Boolean led) {
        this.led = led;
    }
}
