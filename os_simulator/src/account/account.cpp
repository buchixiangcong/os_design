#include "account/account.h"
#include <sstream>
#include <iomanip>

// ============================================================
// 简单密码哈希（基于字符累加的简易哈希，演示用途）
// ============================================================

std::string AccountManager::hash_password(const std::string& password) {
    // 这是一个简化的哈希，实际项目中应使用 SHA256
    unsigned long hash = 5381;
    for (char c : password) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << hash;
    return oss.str();
}

// ============================================================
// 注册
// ============================================================

std::string AccountManager::register_user(const std::string& username,
                                           const std::string& password) {
    LockGuard lock(mutex_);

    // 校验用户名
    if (username.empty()) {
        return "[错误] 用户名不能为空。";
    }
    if (username.find(' ') != std::string::npos) {
        return "[错误] 用户名不能包含空格。";
    }

    // 检查是否已存在
    if (users_.find(username) != users_.end()) {
        return "[错误] 用户名 '" + username + "' 已存在。";
    }

    // 校验密码
    if (password.empty()) {
        return "[错误] 密码不能为空。";
    }

    // 创建用户
    users_[username] = UserInfo(username, hash_password(password));
    return ""; // 成功
}

// ============================================================
// 登录
// ============================================================

std::string AccountManager::login(const std::string& username,
                                   const std::string& password) {
    LockGuard lock(mutex_);

    // 如果已经登录
    if (logged_in_) {
        return "[错误] 已登录为 '" + current_user_ + "'，请先登出。";
    }

    // 查找用户
    auto it = users_.find(username);
    if (it == users_.end()) {
        return "[错误] 用户 '" + username + "' 不存在。";
    }

    UserInfo& info = it->second;

    // 检查是否被锁定
    if (info.locked) {
        return "[错误] 账户已被锁定，请联系管理员。";
    }

    // 验证密码
    if (info.password_hash != hash_password(password)) {
        ++info.failed_attempts;
        if (info.failed_attempts >= MAX_LOGIN_ATTEMPTS) {
            info.locked = true;
            return "[错误] 密码错误次数过多 (" + std::to_string(MAX_LOGIN_ATTEMPTS)
                   + ")，账户已被锁定！";
        }
        return "[错误] 密码错误 (" + std::to_string(info.failed_attempts)
               + "/" + std::to_string(MAX_LOGIN_ATTEMPTS) + ")。";
    }

    // 登录成功
    info.failed_attempts = 0;
    current_user_ = username;
    logged_in_ = true;

    return ""; // 成功
}

// ============================================================
// 登出
// ============================================================

void AccountManager::logout() {
    LockGuard lock(mutex_);
    logged_in_ = false;
    current_user_.clear();
}

// ============================================================
// 查询
// ============================================================

std::vector<std::string> AccountManager::get_all_users() const {
    LockGuard lock(mutex_);
    std::vector<std::string> result;
    for (const auto& kv : users_) {
        result.push_back(kv.first);
    }
    return result;
}
