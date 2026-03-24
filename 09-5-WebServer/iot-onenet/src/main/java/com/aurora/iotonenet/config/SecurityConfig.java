package com.aurora.iotonenet.config;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configuration.EnableWebSecurity;
import org.springframework.security.web.SecurityFilterChain;

/**
 * Spring Security配置
 * 配置登录页面和静态资源访问权限
 */
@Configuration
@EnableWebSecurity
public class SecurityConfig {

    @Bean
    public SecurityFilterChain securityFilterChain(HttpSecurity http) throws Exception {
        http
            .csrf(csrf -> csrf.disable())
            .authorizeHttpRequests(auth -> auth
                // 允许访问登录页面和登录API
                .requestMatchers("/login.html", "/api/login", "/api/check-login").permitAll()
                // 允许访问静态资源（CSS、JS、图标等）
                .requestMatchers("/css/**", "/js/**", "/images/**", "/favicon.ico").permitAll()
                // 其他所有请求都需要认证
                .anyRequest().permitAll()  // 暂时允许所有请求，可根据需要改为 authenticated()
            );
        return http.build();
    }
}
