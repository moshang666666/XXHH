package com.aurora.iotonenet.services;

import com.aurora.iotonenet.config.OneNetProperties;
import com.aurora.iotonenet.dto.OneNetApiResult;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * OneNET Open API 服务
 * 封装对OneNET平台的API调用
 */
@Service
public class OneNetApiService {
    
    private static final Logger logger = LoggerFactory.getLogger(OneNetApiService.class);
    
    private final OneNetProperties properties;
    private final ObjectMapper objectMapper;

    public OneNetApiService(OneNetProperties properties, ObjectMapper objectMapper) {
        this.properties = properties;
        this.objectMapper = objectMapper;
    }

    /**
     * 设置LED状态
     */
    public OneNetApiResult setLedProperty(String deviceName, boolean led) {
        String url = properties.getPropertySetUrl();
        String productId = properties.getProductId();
        String auth = properties.getAuthorization();
        
        if (url == null || productId == null || deviceName == null) {
            logger.error("OneNET配置不完整: url={}, productId={}, deviceName={}", url, productId, deviceName);
            return new OneNetApiResult(false, null, -1, "配置不完整");
        }
        
        HttpURLConnection conn = null;
        try {
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(properties.getTimeoutMs());
            conn.setReadTimeout(properties.getTimeoutMs());
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            
            if (auth != null && !auth.trim().isEmpty()) {
                conn.setRequestProperty("Authorization", auth);
                conn.setRequestProperty("authorization", auth);
            }
            
            // 构建请求体
            ObjectNode body = objectMapper.createObjectNode();
            body.put("product_id", productId);
            body.put("device_name", deviceName);
            ObjectNode params = objectMapper.createObjectNode();
            params.put("Led", led);
            body.set("params", params);
            
            byte[] bytes = objectMapper.writeValueAsBytes(body);
            try (OutputStream os = conn.getOutputStream()) {
                os.write(bytes);
            }
            
            int code = conn.getResponseCode();
            String resp = readResponse(conn);
            
            logger.info("OneNET API调用完成: httpCode={}, response={}", code, resp);
            
            // 解析响应
            String opId = null;
            boolean success = false;
            try {
                JsonNode obj = objectMapper.readTree(resp);
                Integer c1 = obj.path("code").asInt(-1);
                JsonNode data = obj.path("data");
                Integer c2 = data.path("code").asInt(-1);
                opId = data.path("id").asText(null);
                success = (code >= 200 && code < 300) && (c1 == 0) && (c2 == 0) && opId != null;
            } catch (Exception e) {
                logger.warn("解析OneNET响应失败", e);
            }
            
            return new OneNetApiResult(success, opId, code, resp);
        } catch (Exception e) {
            logger.error("调用OneNET API异常", e);
            return new OneNetApiResult(false, null, -1, e.getMessage());
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    private String readResponse(HttpURLConnection conn) {
        try (BufferedReader br = new BufferedReader(new InputStreamReader(
                (conn.getErrorStream() != null ? conn.getErrorStream() : conn.getInputStream()), 
                StandardCharsets.UTF_8))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line);
            }
            return sb.toString();
        } catch (Exception e) {
            logger.error("读取响应失败", e);
            return "";
        }
    }
}
