package com.aurora.iotonenet.config;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.ViewControllerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

/**
 * Web MVC配置
 * 配置静态资源访问和前端路由
 */
@Configuration
public class WebMvcConfig implements WebMvcConfigurer {

    @Override
    public void addResourceHandlers(ResourceHandlerRegistry registry) {
        // 静态资源映射
        registry.addResourceHandler("/**")
                .addResourceLocations(
                        "classpath:/static/",
                        "classpath:/frontend/",
                        "file:./frontend/",
                        "file:./src/main/frontend/"
                );
    }

    @Override
    public void addViewControllers(ViewControllerRegistry registry) {
        // 根路径映射到index.html
        registry.addViewController("/").setViewName("forward:/index.html");
    }
}
