#pragma once
#include <json/json.h>
#include <string>

struct pg_conn;

namespace jkmart {

class Db {
public:
    static Db& instance();

    bool connect();
    bool isConnected() const { return conn_ != nullptr; }
    void disconnect();

    Json::Value query(const std::string& sql);
    std::string lastError() const { return lastError_; }

private:
    Db() = default;
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    pg_conn* conn_ = nullptr;
    std::string lastError_;
};

}