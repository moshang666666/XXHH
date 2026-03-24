package com.aurora.iotonenet.consumer;

import com.aurora.iotonenet.services.DeviceIntegrationService;
import org.springframework.stereotype.Component;

@Component
public class DeviceMessageHandler {
    
    private final DeviceIntegrationService integrationService;

    public DeviceMessageHandler(DeviceIntegrationService integrationService) {
        this.integrationService = integrationService;
    }

    /**
     * 处理设备上报的属性数据
     * 直接调用集成服务
     */
    public void handlePropertyReport(String deviceId, String deviceName, 
                                    Double temperature, Double humidity, 
                                    Double light, Double mq2, Integer error, Boolean led) {
        // 所有业务逻辑已封装在service中
        integrationService.handleDeviceData(deviceId, deviceName, temperature, humidity, light, mq2, error, led);
    }

    /**
     * 处理设备的set_reply消息
     * 直接调用集成服务
     */
    public void handleSetReply(String deviceId, String requestId, 
                              boolean success, String message) {
        integrationService.handleSetReply(deviceId, requestId, success, message);
    }

    /**
     * 当无法获取deviceId时,仅通过requestId处理set_reply
     */
    public void handleSetReplyByRequestIdOnly(String requestId, 
                                             boolean success, String message) {
        integrationService.handleSetReplyByRequestId(requestId, success, message);
    }
}
