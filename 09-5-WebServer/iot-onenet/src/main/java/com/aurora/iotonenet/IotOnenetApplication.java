package com.aurora.iotonenet;

import com.aurora.iotonenet.consumer.IoTPulsarConsume;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.autoconfigure.jdbc.DataSourceAutoConfiguration;
import org.springframework.context.annotation.Bean;

@SpringBootApplication(exclude = {DataSourceAutoConfiguration.class})
public class IotOnenetApplication {

    private static final Logger logger = LoggerFactory.getLogger(IotOnenetApplication.class);

    public static void main(String[] args) {
        logger.info("正在启动 IoT OneNET 应用...");
        SpringApplication.run(IotOnenetApplication.class, args);
    }

    /**
     * 在Spring Boot启动完成后自动启动Pulsar消费者
     */
    @Bean
    public CommandLineRunner startPulsarConsumer(IoTPulsarConsume pulsarConsume) {
        return args -> {
            logger.info("Spring Boot 启动完成，正在初始化 Pulsar 消费者...");
            // 在新线程中启动Pulsar消费者，避免阻塞主线程
            new Thread(() -> {
                try {
                    pulsarConsume.startConsuming();
                } catch (Exception e) {
                    logger.error("Pulsar消费者启动失败", e);
                }
            }, "pulsar-consumer-thread").start();
        };
    }
}
