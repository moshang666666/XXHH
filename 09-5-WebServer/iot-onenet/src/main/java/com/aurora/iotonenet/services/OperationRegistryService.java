package com.aurora.iotonenet.services;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 操作注册表服务
 * 管理待完成的操作请求,跟踪操作状态
 */
@Service
public class OperationRegistryService {
    
    private static final Logger logger = LoggerFactory.getLogger(OperationRegistryService.class);
    private static final long TTL_MS = 10_000L; // 10秒超时

    private static class OperationRecord {
        final String deviceId;
        final boolean desired;
        final long createdAt;
        volatile boolean done;
        volatile boolean success;
        volatile String message;

        OperationRecord(String deviceId, boolean desired) {
            this.deviceId = deviceId;
            this.desired = desired;
            this.createdAt = System.currentTimeMillis();
        }
    }

    private final Map<String, OperationRecord> pending = new ConcurrentHashMap<>();
    private final Map<String, String> requestIdIndex = new ConcurrentHashMap<>();

    /**
     * 注册新操作并生成requestId
     */
    public String register(String deviceId, boolean desiredLed) {
        cleanup();
        String requestId = UUID.randomUUID().toString();
        doRegister(deviceId, desiredLed, requestId);
        return requestId;
    }

    /**
     * 使用指定的requestId注册操作
     */
    public void registerWith(String deviceId, boolean desiredLed, String requestId) {
        if (deviceId == null || requestId == null) {
            return;
        }
        cleanup();
        doRegister(deviceId, desiredLed, requestId);
    }

    private void doRegister(String deviceId, boolean desiredLed, String requestId) {
        String key = makeKey(deviceId, requestId);
        pending.put(key, new OperationRecord(deviceId, desiredLed));
        requestIdIndex.put(requestId, deviceId);
        logger.debug("操作已注册: deviceId={}, requestId={}, desired={}", deviceId, requestId, desiredLed);
    }

    /**
     * 完成操作
     */
    public void complete(String deviceId, String requestId, boolean success, String message) {
        if (deviceId == null || requestId == null) {
            return;
        }
        
        String key = makeKey(deviceId, requestId);
        OperationRecord record = pending.remove(key);
        if (record != null) {
            record.done = true;
            record.success = success;
            record.message = message;
            logger.info("操作已完成: deviceId={}, requestId={}, success={}, message={}", 
                       deviceId, requestId, success, message);
        }
        requestIdIndex.remove(requestId);
    }

    /**
     * 仅通过requestId完成操作
     */
    public void completeByRequestId(String requestId, boolean success, String message) {
        if (requestId == null) {
            return;
        }
        
        String deviceId = requestIdIndex.remove(requestId);
        if (deviceId != null) {
            complete(deviceId, requestId, success, message);
        }
    }

    /**
     * 清理超时的操作记录
     */
    private void cleanup() {
        long now = System.currentTimeMillis();
        pending.entrySet().removeIf(entry -> {
            OperationRecord record = entry.getValue();
            boolean expired = now - record.createdAt > TTL_MS;
            if (expired) {
                logger.debug("操作记录超时已清理: key={}", entry.getKey());
            }
            return expired;
        });
        
        // 清理索引
        requestIdIndex.entrySet().removeIf(entry -> {
            String reqId = entry.getKey();
            String devId = entry.getValue();
            return !pending.containsKey(makeKey(devId, reqId));
        });
    }

    private String makeKey(String deviceId, String requestId) {
        return deviceId + "|" + requestId;
    }
}
