#include "Auth.h"

#include "Crypto.h"
#include "Db.h"

#include <mutex>
#include <unordered_map>

namespace jkmart {

namespace {

User userFromJson(const Json::Value& row) {
    User u;
    if (row.isMember("id")) u.id = row["id"].asString();
    if (row.isMember("name")) u.name = row["name"].asString();
    if (row.isMember("email")) u.email = row["email"].asString();
    if (row.isMember("role")) u.role = row["role"].asString();
    if (row.isMember("password_hash")) u.passwordHash = row["password_hash"].asString();
    if (row.isMember("is_active")) {
        u.isActive = row["is_active"].asString() == "t";
    }
    return u;
}

std::mutex& sessionMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, User>& sessions() {
    static std::unordered_map<std::string, User> s;
    return s;
}

bool validRole(const std::string& role) {
    return role == "BUYER" || role == "SELLER" || role == "ADMIN";
}

}  // namespace

bool authRegister(const std::string& name,
                  const std::string& email,
                  const std::string& password,
                  const std::string& role,
                  User& outUser,
                  std::string& err) {
    auto& db = Db::instance();

    std::string finalRole = role;
    if (finalRole.empty()) finalRole = "BUYER";
    if (finalRole != "BUYER" && finalRole != "SELLER") {
        err = "invalid role (BUYER or SELLER only)";
        return false;
    }

    auto existing = db.query(
        "SELECT id FROM users WHERE email = $1::text LIMIT 1", {email});
    if (db.lastErrorSet()) {
        err = db.lastError();
        return false;
    }
    if (!existing.empty()) {
        err = "email already registered";
        return false;
    }

    const std::string hash = hashPassword(password);
    auto rows = db.query(
        "INSERT INTO users (name, email, password_hash, role) "
        "VALUES ($1::text, $2::text, $3::text, $4::text) "
        "RETURNING id, name, email, role, password_hash, is_active",
        {name, email, hash, finalRole});
    if (rows.empty()) {
        err = "insert failed: " + db.lastError();
        return false;
    }
    outUser = userFromJson(rows[0]);
    err.clear();
    return true;
}

bool authLogin(const std::string& email,
               const std::string& password,
               User& outUser,
               std::string& err) {
    auto& db = Db::instance();
    auto rows = db.query(
        "SELECT id, name, email, role, password_hash, is_active "
        "FROM users WHERE email = $1::text LIMIT 1",
        {email});
    if (rows.empty()) {
        err = "invalid email or password";
        return false;
    }
    outUser = userFromJson(rows[0]);
    if (!verifyPassword(password, outUser.passwordHash)) {
        err = "invalid email or password";
        return false;
    }
    if (!outUser.isActive) {
        err = "account is suspended";
        return false;
    }
    err.clear();
    return true;
}

std::string createSession(const User& user) {
    std::string token = randomHex(32);
    std::lock_guard<std::mutex> lock(sessionMutex());
    sessions()[token] = user;
    return token;
}

bool userByToken(const std::string& token, User& user) {
    std::lock_guard<std::mutex> lock(sessionMutex());
    auto it = sessions().find(token);
    if (it == sessions().end()) {
        return false;
    }
    user = it->second;
    return true;
}

void destroySession(const std::string& token) {
    std::lock_guard<std::mutex> lock(sessionMutex());
    sessions().erase(token);
}

bool hasRole(const User& user, const std::string& role) {
    return validRole(role) && user.role == role;
}

}  // namespace jkmart