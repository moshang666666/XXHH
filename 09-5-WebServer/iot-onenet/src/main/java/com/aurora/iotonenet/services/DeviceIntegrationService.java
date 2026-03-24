package com.aurora.iotonenet.services;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

/**
 * IoT设备集成服务
 * 提供统一的设备数据处理接口,供消息消费者调用
 */
@Service
public class DeviceIntegrationService {
    
    private static final Logger logger = LoggerFactory.getLogger(DeviceIntegrationService.class);
    
    private final DeviceStateService deviceStateService;
    private final OperationRegistryService operationRegistryService;

    public DeviceIntegrationService(DeviceStateService deviceStateService,
                                   OperationRegistryService operationRegistryService) {
        this.deviceStateService = deviceStateService;
        this.operationRegistryService = operationRegistryService;
    }

    /**
     * 处理设备上报的数据
     * 供Pulsar消费者或其他消息处理组件调用
     */
    public void handleDeviceData(String deviceId, String deviceName, 
                                 Double temperature, Double humidity, 
                                 Double light, Double mq2, Integer error, Boolean led) {
        long timestamp = System.currentTimeMillis();
        deviceStateService.updateState(deviceId, deviceName, temperature, humidity, light, mq2, error, led, timestamp);
        logger.info("处理设备数据: deviceId={}, deviceName={}, temp={}, hum={}, light={}, mq2={}, error={}, led={}", 
                   deviceId, deviceName, temperature, humidity, light, mq2, error, led);
    }

    /**
     * 处理设备的set_reply响应
     * 供消息消费者在收到设备确认时调用
     */
    public void handleSetReply(String deviceId, String requestId, boolean success, String message) {
        if (requestId != null) {
            operationRegistryService.complete(deviceId, requestId, success, message);
            logger.info("处理set_reply: deviceId={}, requestId={}, success={}, message={}", 
                       deviceId, requestId, success, message);
        }
    }

    /**
     * 仅通过requestId处理set_reply
     * 用于无法获取deviceId的场景
     */
    public void handleSetReplyByRequestId(String requestId, boolean success, String message) {
        if (requestId != null) {
            operationRegistryService.completeByRequestId(requestId, success, message);
            logger.info("通过requestId处理set_reply: requestId={}, success={}, message={}", 
                       requestId, success, message);
        }
    }
}
