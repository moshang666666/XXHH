package com.aurora.iotonenet.services;

import com.aurora.iotonenet.dto.DeviceStateDTO;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * 设备状态管理服务
 * 线程安全地存储和获取最新的设备状态数据
 */
@Service
public class DeviceStateService {
    
    private static final Logger logger = LoggerFactory.getLogger(DeviceStateService.class);
    private final ObjectMapper objectMapper;
    private final ReadWriteLock lock = new ReentrantReadWriteLock();
    
    private DeviceStateDTO currentState;

    public DeviceStateService(ObjectMapper objectMapper) {
        this.objectMapper = objectMapper;
        // 初始化空状态
        this.currentState = new DeviceStateDTO();
    }

    /**
     * 更新设备状态
     */
    public void updateState(String deviceId, String deviceName, 
                           Double temperature, Double humidity, 
                           Double light, Double mq2, Integer error,
                           Boolean led, long updatedAt) {
        lock.writeLock().lock();
        try {
            DeviceStateDTO state = new DeviceStateDTO();
            state.setDeviceId(deviceId);
            state.setDeviceName(deviceName);
            state.setTemperature(new DeviceStateDTO.SensorValue(temperature, "°C"));
            state.setHumidity(new DeviceStateDTO.SensorValue(humidity, "%"));
            state.setLight(new DeviceStateDTO.SensorValue(light, "Lux"));
            state.setMq2(new DeviceStateDTO.SensorValue(mq2, "ppm"));
            state.setError(new DeviceStateDTO.ErrorValue(error));
            state.setLed(new DeviceStateDTO.LedValue(led));
            state.setUpdatedAt(updatedAt);
            
            this.currentState = state;
            logger.info("设备状态已更新: deviceId={}, deviceName={}, temp={}, hum={}, light={}, mq2={}, error={}, led={}", 
                       deviceId, deviceName, temperature, humidity, light, mq2, error, led);
        } finally {
            lock.writeLock().unlock();
        }
    }

    /**
     * 获取当前设备状态
     */
    public DeviceStateDTO getCurrentState() {
        lock.readLock().lock();
        try {
            return currentState;
        } finally {
            lock.readLock().unlock();
        }
    }

    /**
     * 获取设备ID
     */
    public String getCurrentDeviceId() {
        lock.readLock().lock();
        try {
            return currentState != null ? currentState.getDeviceId() : null;
        } finally {
            lock.readLock().unlock();
        }
    }

    /**
     * 获取设备名称
     */
    public String getCurrentDeviceName() {
        lock.readLock().lock();
        try {
            return currentState != null ? currentState.getDeviceName() : null;
        } finally {
            lock.readLock().unlock();
        }
    }
}
