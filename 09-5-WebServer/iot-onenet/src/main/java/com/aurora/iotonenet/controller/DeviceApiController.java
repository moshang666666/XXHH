package com.aurora.iotonenet.controller;

import com.aurora.iotonenet.dto.DeviceStateDTO;
import com.aurora.iotonenet.dto.LedOperationRequest;
import com.aurora.iotonenet.dto.LedOperationResponse;
import com.aurora.iotonenet.dto.OneNetApiResult;
import com.aurora.iotonenet.services.DeviceStateService;
import com.aurora.iotonenet.services.OneNetApiService;
import com.aurora.iotonenet.services.OperationRegistryService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

/**
 * IoT设备API控制器
 * 提供设备状态查询和LED控制接口
 */
@RestController
@RequestMapping("/api")
@CrossOrigin(origins = "*")
public class DeviceApiController {
    
    private static final Logger logger = LoggerFactory.getLogger(DeviceApiController.class);
    
    private final DeviceStateService deviceStateService;
    private final OneNetApiService oneNetApiService;
    private final OperationRegistryService operationRegistryService;

    public DeviceApiController(DeviceStateService deviceStateService,
                               OneNetApiService oneNetApiService,
                               OperationRegistryService operationRegistryService) {
        this.deviceStateService = deviceStateService;
        this.oneNetApiService = oneNetApiService;
        this.operationRegistryService = operationRegistryService;
    }

    /**
     * 获取设备当前状态
     */
    @GetMapping("/status")
    public ResponseEntity<DeviceStateDTO> getDeviceStatus() {
        DeviceStateDTO state = deviceStateService.getCurrentState();
        logger.info("API请求 /api/status，返回设备状态: deviceId={}, deviceName={}, temp={}, hum={}, light={}, mq2={}, error={}", 
                   state.getDeviceId(), state.getDeviceName(), 
                   state.getTemperature() != null ? state.getTemperature().getValue() : null,
                   state.getHumidity() != null ? state.getHumidity().getValue() : null,
                   state.getLight() != null ? state.getLight().getValue() : null,
                   state.getMq2() != null ? state.getMq2().getValue() : null,
                   state.getError() != null ? state.getError().getValue() : null);
        return ResponseEntity.ok(state);
    }

    /**
     * 设置LED状态
     */
    @PostMapping("/ops/led")
    public ResponseEntity<LedOperationResponse> setLed(@RequestBody LedOperationRequest request) {
        String deviceId = request.getDeviceId();
        String deviceName = request.getDeviceName();
        Boolean led = request.getLed();
        
        // 验证必填参数
        if (deviceId == null || deviceId.trim().isEmpty() || led == null) {
            logger.warn("LED操作请求参数不完整: deviceId={}, led={}", deviceId, led);
            return ResponseEntity.badRequest()
                    .body(new LedOperationResponse("error", null, "deviceId和led参数不能为空"));
        }
        
        // 确定要使用的设备名称
        String effectiveDeviceName = deviceName;
        if (effectiveDeviceName == null || effectiveDeviceName.trim().isEmpty()) {
            effectiveDeviceName = deviceStateService.getCurrentDeviceName();
        }
        if (effectiveDeviceName == null || effectiveDeviceName.trim().isEmpty()) {
            effectiveDeviceName = deviceId;
        }
        
        logger.info("执行LED操作: deviceId={}, deviceName={}, led={}", deviceId, effectiveDeviceName, led);
        
        // 调用OneNET API
        OneNetApiResult apiResult = oneNetApiService.setLedProperty(effectiveDeviceName, led);
        
        String requestId;
        String status;
        String message;
        
        if (apiResult != null && apiResult.isSuccess() && apiResult.getOperationId() != null) {
            // OneNET API调用成功
            requestId = apiResult.getOperationId();
            status = "accepted";
            message = "已通过OneNET API下发LED指令";
            operationRegistryService.registerWith(deviceId, led, requestId);
        } else {
            // OneNET API调用失败,使用备用方案
            requestId = operationRegistryService.register(deviceId, led);
            status = "pending";
            message = "OneNET API调用失败,已登记到待处理队列: " + 
                     (apiResult != null ? apiResult.getBody() : "未知错误");
            logger.warn("OneNET API调用失败: httpCode={}, body={}", 
                       apiResult != null ? apiResult.getHttpCode() : -1,
                       apiResult != null ? apiResult.getBody() : "null");
        }
        
        LedOperationResponse response = new LedOperationResponse(status, requestId, message);
        return ResponseEntity.status(HttpStatus.ACCEPTED).body(response);
    }

    /**
     * 健康检查接口
     */
    @GetMapping("/health")
    public ResponseEntity<String> health() {
        return ResponseEntity.ok("ok");
    }
}
