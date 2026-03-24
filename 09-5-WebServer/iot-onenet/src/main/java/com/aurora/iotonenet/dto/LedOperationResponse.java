package com.aurora.iotonenet.dto;

/**
 * LED操作响应DTO
 */
public class LedOperationResponse {
    
    private String status;
    private String requestId;
    private String message;

    public LedOperationResponse() {}

    public LedOperationResponse(String status, String requestId, String message) {
        this.status = status;
        this.requestId = requestId;
        this.message = message;
    }

    public String getStatus() {
        return status;
    }

    public void setStatus(String status) {
        this.status = status;
    }

    public String getRequestId() {
        return requestId;
    }

    public void setRequestId(String requestId) {
        this.requestId = requestId;
    }

    public String getMessage() {
        return message;
    }

    public void setMessage(String message) {
        this.message = message;
    }
}
