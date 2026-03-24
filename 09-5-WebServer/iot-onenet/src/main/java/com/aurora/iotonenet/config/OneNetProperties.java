package com.aurora.iotonenet.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;

/**
 * OneNET 平台配置属性
 */
@Component
@ConfigurationProperties(prefix = "onenet")
public class OneNetProperties {
    
    /**
     * 产品ID
     */
    private String productId;
    
    /**
     * 授权Token
     */
    private String authorization;
    
    /**
     * 属性设置接口URL
     */
    private String propertySetUrl;
    
    /**
     * 属性查询接口URL
     */
    private String propertyGetUrl;
    
    /**
     * 连接超时时间(毫秒)
     */
    private int timeoutMs = 10000;

    public String getProductId() {
        return productId;
    }

    public void setProductId(String productId) {
        this.productId = productId;
    }

    public String getAuthorization() {
        return authorization;
    }

    public void setAuthorization(String authorization) {
        this.authorization = authorization;
    }

    public String getPropertySetUrl() {
        return propertySetUrl;
    }

    public void setPropertySetUrl(String propertySetUrl) {
        this.propertySetUrl = propertySetUrl;
    }

    public String getPropertyGetUrl() {
        return propertyGetUrl;
    }

    public void setPropertyGetUrl(String propertyGetUrl) {
        this.propertyGetUrl = propertyGetUrl;
    }

    public int getTimeoutMs() {
        return timeoutMs;
    }

    public void setTimeoutMs(int timeoutMs) {
        this.timeoutMs = timeoutMs;
    }
}
