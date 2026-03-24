package com.aurora.iotonenet.consumer;

import com.alibaba.fastjson2.JSONObject;
import com.aurora.iotonenet.config.IoTConfig;
import com.aurora.iotonenet.auth.IoTConsumer;
import com.aurora.iotonenet.auth.IoTMessage;
import com.aurora.iotonenet.auth.AESBase64Utils;
import com.aurora.iotonenet.services.DeviceIntegrationService;
import io.netty.util.internal.StringUtil;
import org.apache.pulsar.client.api.MessageId;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.awt.Desktop;
import java.io.File;
import java.net.URI;
import java.util.ArrayList;
import java.util.List;

/**
 * Pulsar消息消费者 (已重构为Spring组件)
 * 1) 通过Spring Boot启动，无需手动启动HTTP服务
 * 2) 自动尝试在默认浏览器打开仪表盘页面
 * 3) 基于 OneNET 提供的 Pulsar 接入消费消息，解密并解析设备上报的 JSON
 * 4) 使用 DeviceIntegrationService 处理设备数据
 */
@Component
public class IoTPulsarConsume {
    private static final Logger logger = LoggerFactory.getLogger(IoTPulsarConsume.class);
    
    private final DeviceIntegrationService integrationService;
    
    @Value("${onenet.pulsar.access-id:20HkZmOFURiMlWCP9614}")
    private String iotAccessId;
    
    @Value("${onenet.pulsar.secret-key:ab2f30137f424f82b5049927f25ee090}")
    private String iotSecretKey;
    
    @Value("${onenet.pulsar.subscription-name:20HkZmOFURiMlWCP9614-sub}")
    private String iotSubscriptionName;
    
    @Value("${server.port:8082}")
    private int serverPort;

    public IoTPulsarConsume(DeviceIntegrationService integrationService) {
        this.integrationService = integrationService;
    }

    /**
     * 启动Pulsar消费者 (由Spring Boot CommandLineRunner调用)
     * - Spring Boot已自动启动HTTP服务
     * - 尝试自动打开前端页面
     * - 检查必要的 OneNET 凭据
     * - 构建并启动 IoTConsumer，接收消息、解密 originalMsg、解析子数据
     * - 使用 DeviceIntegrationService 处理设备数据
     */
    public void startConsuming() throws Exception {
        logger.info("HTTP服务已由Spring Boot启动在 http://127.0.0.1:{}", serverPort);
        logger.info("API接口: http://127.0.0.1:{}/api/status", serverPort);
        
        // 尝试自动打开前端仪表盘页面
        tryOpenDashboard();
        if (StringUtil.isNullOrEmpty(iotAccessId)) {
            logger.error("iotAccessId is null,please input iotAccessId");
            System.exit(1);
        }
        if (StringUtil.isNullOrEmpty(iotSecretKey)) {
            logger.error("iotSecretKey is null,please input iotSecretKey");
            System.exit(1);
        }
        if (StringUtil.isNullOrEmpty(iotSubscriptionName)) {
            logger.error("iotSubscriptionName is null,please input iotSubscriptionName");
            System.exit(1);
        }
        //收到消息后将消息转到中间件后立即ACK。避免消息量过大导致消息过期
        logger.info("正在创建Pulsar消费者: brokerUrl={}, accessId={}, subscription={}", 
                   IoTConfig.brokerSSLServerUrl, iotAccessId, iotSubscriptionName);
        IoTConsumer iotConsumer = IoTConsumer.IOTConsumerBuilder.anIOTConsumer().brokerServerUrl(IoTConfig.brokerSSLServerUrl)
                .iotAccessId(iotAccessId)
                .iotSecretKey(iotSecretKey)
                .subscriptionName(iotSubscriptionName)
                .iotMessageListener(message -> {
                    // 回调线程：处理每一条到达的消息（建议尽快 ACK）
                    MessageId msgId = message.getMessageId();
                    long publishTime = message.getPublishTime();
                    String payload = new String(message.getData());
                    logger.info("【Pulsar消息到达】messageId={}, publishTime={}, payload={}", msgId, publishTime, payload);
                    
                    IoTMessage iotMessage= JSONObject.parseObject(payload, IoTMessage.class);
                    // originalMsg 使用 AES 解密得到真正的设备上报内容
                    String originalMsg= AESBase64Utils.decrypt(iotMessage.getData(),iotSecretKey.substring(8,24));
                    logger.info("【解密后的消息】originalMsg: {}", originalMsg);

                    // 解析 originalMsg：
                    // 1) 若是属性设置响应（set_reply），则尝试以 requestId 完成 PendingOps；
                    // 2) 否则按遥测解析温度/湿度/LED，并写入状态存储。
                    try {
                        JSONObject obj = JSONObject.parseObject(originalMsg);
                        logger.debug("【解析JSON】成功解析消息对象");
                        // 简单判断是否为 set_reply：存在 id + code 字段，且没有 subData
                        if (obj.containsKey("id") && obj.containsKey("code") && !obj.containsKey("subData")) {
                            String replyId = obj.getString("id");
                            Integer code = obj.getInteger("code");
                            String msg = obj.getString("msg");
                            // 设备ID最佳来源依赖平台消息体，这里尽力尝试从字段或外层推断
                            String deviceId = obj.getString("deviceId");
                            if (deviceId == null) {
                                // 如果消息未提供 deviceId，使用登记表按 requestId 回填
                                boolean success = (code != null && code == 0);
                                integrationService.handleSetReplyByRequestId(replyId, success, msg);
                                logger.info("set_reply completed by requestId -> id={}, success={}, msg={}", replyId, success, msg);
                            } else {
                                boolean success = (code != null && code == 0);
                                integrationService.handleSetReply(deviceId, replyId, success, msg);
                                logger.info("set_reply completed -> deviceId={}, id={}, success={}, msg={}", deviceId, replyId, success, msg);
                            }
                        } else {
                            JSONObject sub = obj.getJSONObject("subData");
                            logger.debug("【消息类型】设备上报数据，subData={}", sub);
                            if (sub != null) {
                            String deviceId = sub.getString("deviceId");
                            String deviceName = sub.getString("deviceName");
                            JSONObject params = sub.getJSONObject("params");
                            Double temp = null;
                            Double hum = null;
                            Double light = null;
                            Double mq2 = null;
                            Integer error = null;
                            Boolean led = null;
                            long ts = System.currentTimeMillis();
                            if (params != null) {
                                // 遍历所有 key，做“弱匹配”：
                                // - 包含 "temp" 视作温度；
                                // - 包含 "hum" 视作湿度；
                                // - 包含 led/lamp/light 视作指示灯；
                                // 每个条目尝试读取 entry.value 或 entry.data
                                for (String key : params.keySet()) {
                                    if (key == null) continue;
                                    JSONObject entry = params.getJSONObject(key);
                                    if (entry == null) continue;

                                    Long time = entry.getLong("time");
                                    if (time != null) {
                                        ts = Math.max(ts, time);
                                    }

                                    Object valueObj = entry.containsKey("value") ? entry.get("value") : entry.get("data");
                                    String lowerKey = key.toLowerCase();

                                    if (lowerKey.contains("temp")) {
                                        Double parsed = parseDouble(valueObj);
                                        if (parsed != null) {
                                            temp = parsed;
                                        }
                                    } else if (lowerKey.contains("hum")) {
                                        Double parsed = parseDouble(valueObj);
                                        if (parsed != null) {
                                            hum = parsed;
                                        }
                                    } else if (lowerKey.contains("light")) {
                                        Double parsed = parseDouble(valueObj);
                                        if (parsed != null) {
                                            light = parsed;
                                        }
                                    } else if (lowerKey.contains("mq2")) {
                                        Double parsed = parseDouble(valueObj);
                                        if (parsed != null) {
                                            mq2 = parsed;
                                        }
                                    } else if (lowerKey.contains("error")) {
                                        Double parsed = parseDouble(valueObj);
                                        if (parsed != null) {
                                            error = parsed.intValue();
                                        }
                                    } else if (lowerKey.contains("led") || lowerKey.contains("lamp")) {
                                        Boolean parsed = parseBoolean(valueObj);
                                        if (parsed != null) {
                                            led = parsed;
                                        }
                                    }
                                }
                            }
                            integrationService.handleDeviceData(deviceId, deviceName, temp, hum, light, mq2, error, led);
                            logger.info("【设备状态已更新】deviceId={}, deviceName={}, temp={}, hum={}, light={}, mq2={}, error={}, led={}, ts={}", 
                                       deviceId, deviceName, temp, hum, light, mq2, error, led, ts);
                            } else {
                                logger.warn("【数据异常】subData为null，无法提取设备信息");
                            }
                        }
                    } catch (Exception ex) {
                        logger.error("【消息解析失败】originalMsg={}, error={}", originalMsg, ex.getMessage(), ex);
                    }
                }).build();
        iotConsumer.run();
    }

    /**
     * 尝试自动打开仪表盘页面：
     * 直接打开 http://127.0.0.1:serverPort/login.html (登录页面)
     * 在不支持 java.awt.Desktop 的环境下，降级为调用系统命令（Windows/macOS/Linux）
     */
    private void tryOpenDashboard() {
        try {
            // Spring Boot会自动serve src/main/resources/static/下的文件
            // 打开登录页面
            String url = "http://127.0.0.1:" + serverPort + "/login.html";
            URI uri = new URI(url);
            
            logger.info("正在打开登录页面: {}", url);
            
            if (Desktop.isDesktopSupported()) {
                try {
                    Desktop.getDesktop().browse(uri);
                    logger.info("仪表盘已在浏览器中打开");
                    return;
                } catch (Exception e) {
                    logger.debug("Desktop.browse失败: {}", e.getMessage());
                }
            }
            
            // Desktop不支持或打开失败，尝试系统命令
            if (tryOpenUriByCommand(url)) {
                logger.info("仪表盘已通过系统命令打开");
            } else {
                logger.info("无法自动打开浏览器，请手动访问: {}", url);
            }
        } catch (Throwable t) {
            logger.warn("Open dashboard failed: {}", t.getMessage());
        }
    }

    /**
     * 使用系统命令打开指定 URI（在不支持 {@link Desktop} 的环境下）。
     * - Windows: cmd /c start "" <uri>
     * - macOS  : open <uri>
     * - Linux  : xdg-open <uri>
     *
     * @param uri 要打开的链接，如 "http://127.0.0.1:8082/" 或 "file:///.../index.html"
     * @return 启动外部进程是否成功（不代表浏览器一定已打开页面）
     */
    private static boolean tryOpenUriByCommand(String uri) {
        try {
            String os = System.getProperty("os.name").toLowerCase();
            ProcessBuilder pb;
            if (os.contains("win")) {
                // Windows: cmd /c start "" "uri"
                pb = new ProcessBuilder("cmd", "/c", "start", "", uri);
            } else if (os.contains("mac")) {
                pb = new ProcessBuilder("open", uri);
            } else {
                // assume linux/unix
                pb = new ProcessBuilder("xdg-open", uri);
            }
            pb.inheritIO();
            Process p = pb.start();
            return true;
        } catch (Throwable e) {
            logger.debug("open by command failed: {}", e.getMessage());
            return false;
        }
    }

    /**
     * 尝试将对象值解析为 Double：
     * - Number 直接转；
     * - String 去空白后用 Double.parseDouble；
     * 其余返回 null。
     */
    private static Double parseDouble(Object value) {
        if (value == null) return null;
        if (value instanceof Number) {
            return ((Number) value).doubleValue();
        }
        if (value instanceof String) {
            String str = ((String) value).trim();
            if (str.isEmpty()) return null;
            try {
                return Double.parseDouble(str);
            } catch (NumberFormatException ignore) {
                return null;
            }
        }
        return null;
    }

    /**
     * 尝试将对象值解析为 Boolean：
     * - Boolean 直接返回；
     * - Number 非 0 视为 true，0 视为 false；
     * - String 忽略大小写地匹配 true/on/yes/1 和 false/off/no/0；
     * 其余返回 null。
     */
    private static Boolean parseBoolean(Object value) {
        if (value == null) return null;
        if (value instanceof Boolean) {
            return (Boolean) value;
        }
        if (value instanceof Number) {
            return ((Number) value).doubleValue() != 0.0;
        }
        if (value instanceof String) {
            String str = ((String) value).trim().toLowerCase();
            if (str.isEmpty()) return null;
            if ("true".equals(str) || "on".equals(str) || "yes".equals(str) || "1".equals(str)) {
                return Boolean.TRUE;
            }
            if ("false".equals(str) || "off".equals(str) || "no".equals(str) || "0".equals(str)) {
                return Boolean.FALSE;
            }
        }
        return null;
    }
}
