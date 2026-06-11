#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <string>
#include <vector>
#include <map>
#include "common/types.h"
#include "common/sync.h"

/// 用户账户信息
struct UserInfo {
    std::string username;
    std::string password_hash;  // 简单哈希（用于演示）
    int failed_attempts;         // 连续失败次数
    bool locked;                 // 是否被锁定

    UserInfo() : failed_attempts(0), locked(false) {}
    UserInfo(const std::string& user, const std::string& pass_hash)
        : username(user), password_hash(pass_hash)
        , failed_attempts(0), locked(false) {}
};

/// 账户管理器
class AccountManager {
public:
    AccountManager() = default;
    ~AccountManager() = default;

    // ========== 账户命令 ==========

    /// 注册新用户
    /// @return 错误信息，空字符串表示成功
    std::string register_user(const std::string& username, const std::string& password);

    /// 用户登录
    /// @return 错误信息，空字符串表示成功
    std::string login(const std::string& username, const std::string& password);

    /// 用户登出
    void logout();

    // ========== 状态查询 ==========

    /// 当前是否已登录
    bool is_logged_in() const { return logged_in_; }

    /// 获取当前登录用户名
    std::string current_user() const { return current_user_; }

    /// 获取所有注册用户列表
    std::vector<std::string> get_all_users() const;

    /// 获取互斥锁
    Mutex& mutex() { return mutex_; }

    // ========== 持久化接口 ==========
    const std::map<std::string, UserInfo>& get_users() const { return users_; }
    void set_users(const std::map<std::string, UserInfo>& u) { users_ = u; }

private:
    std::map<std::string, UserInfo> users_;  // 用户名 -> 用户信息
    std::string current_user_;
    bool logged_in_ = false;
    mutable Mutex mutex_;

    /// 简单密码哈希（实际项目应使用 SHA256 等）
    static std::string hash_password(const std::string& password);
};

#endif // ACCOUNT_H
