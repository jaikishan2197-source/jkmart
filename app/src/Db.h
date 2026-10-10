#pragma once
#include <json/json.h>
#include <string>
#include <vector>

struct pg_conn;

namespace jkmart {

class Db {
public:
    static Db& instance();

    bool connect();
    bool isConnected() const { return conn_ != nullptr; }
    void disconnect();

    Json::Value query(const std::string& sql);
    Json::Value query(const std::string& sql,
                      const std::vector<std::string>& params);
    bool exec(const std::string& sql);
    std::string lastError() const { return lastError_; }
    bool lastErrorSet() const { return !lastError_.empty(); }

private:
    Db() = default;
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    pg_conn* conn_ = nullptr;
    std::string lastError_;
};

}