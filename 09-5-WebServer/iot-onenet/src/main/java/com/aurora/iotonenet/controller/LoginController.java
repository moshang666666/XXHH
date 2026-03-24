package com.aurora.iotonenet.controller;

import com.aurora.iotonenet.dto.LoginRequest;
import com.aurora.iotonenet.dto.LoginResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import jakarta.servlet.http.HttpSession;
import java.util.HashMap;
import java.util.Map;

/**
 * 登录控制器
 * 处理用户登录验证和会话管理
 */
@RestController
@RequestMapping("/api")
@CrossOrigin(originPatterns = "*", allowCredentials = "true")
public class LoginController {

    private static final Logger logger = LoggerFactory.getLogger(LoginController.class);
    private static final String SESSION_USER_KEY = "logged_in_user";

    @Value("${app.login.username}")
    private String configUsername;

    @Value("${app.login.password}")
    private String configPassword;

    /**
     * 登录接口
     */
    @PostMapping("/login")
    public ResponseEntity<LoginResponse> login(@RequestBody LoginRequest request, HttpSession session) {
        String username = request.getUsername();
        String password = request.getPassword();

        logger.info("登录尝试: username={}", username);

        if (username == null || password == null || username.trim().isEmpty() || password.trim().isEmpty()) {
            return ResponseEntity.badRequest()
                    .body(new LoginResponse(false, "用户名和密码不能为空"));
        }

        // 验证用户名和密码
        if (configUsername.equals(username) && configPassword.equals(password)) {
            // 登录成功，设置会话
            session.setAttribute(SESSION_USER_KEY, username);
            logger.info("用户登录成功: {}", username);
            return ResponseEntity.ok(new LoginResponse(true, "登录成功"));
        } else {
            logger.warn("登录失败: username={}", username);
            return ResponseEntity.ok(new LoginResponse(false, "用户名或密码错误"));
        }
    }

    /**
     * 检查登录状态
     */
    @GetMapping("/check-login")
    public ResponseEntity<Map<String, Object>> checkLogin(HttpSession session) {
        Map<String, Object> response = new HashMap<>();
        Object user = session.getAttribute(SESSION_USER_KEY);
        
        if (user != null) {
            response.put("loggedIn", true);
            response.put("username", user);
        } else {
            response.put("loggedIn", false);
        }
        
        return ResponseEntity.ok(response);
    }

    /**
     * 登出接口
     */
    @PostMapping("/logout")
    public ResponseEntity<LoginResponse> logout(HttpSession session) {
        Object user = session.getAttribute(SESSION_USER_KEY);
        if (user != null) {
            logger.info("用户登出: {}", user);
            session.invalidate();
        }
        return ResponseEntity.ok(new LoginResponse(true, "已登出"));
    }
}
