package com.aurora.iotonenet.dto;

/**
 * OneNET API调用结果
 */
public class OneNetApiResult {
    
    private boolean success;
    private String operationId;
    private int httpCode;
    private String body;

    public OneNetApiResult() {}

    public OneNetApiResult(boolean success, String operationId, int httpCode, String body) {
        this.success = success;
        this.operationId = operationId;
        this.httpCode = httpCode;
        this.body = body;
    }

    public boolean isSuccess() {
        return success;
    }

    public void setSuccess(boolean success) {
        this.success = success;
    }

    public String getOperationId() {
        return operationId;
    }

    public void setOperationId(String operationId) {
        this.operationId = operationId;
    }

    public int getHttpCode() {
        return httpCode;
    }

    public void setHttpCode(int httpCode) {
        this.httpCode = httpCode;
    }

    public String getBody() {
        return body;
    }

    public void setBody(String body) {
        this.body = body;
    }
}
