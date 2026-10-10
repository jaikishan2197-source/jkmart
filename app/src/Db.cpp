#include "Db.h"
#include <libpq-fe.h>
#include <cstdlib>
#include <sstream>

namespace jkmart {

Db& Db::instance() {
    static Db d;
    return d;
}

Db::~Db() { disconnect(); }

bool Db::connect() {
    disconnect();
    const char* host = std::getenv("PGHOST");
    const char* port = std::getenv("PGPORT");
    const char* db   = std::getenv("PGDATABASE");
    const char* user = std::getenv("PGUSER");
    const char* pass = std::getenv("PGPASSWORD");

    std::ostringstream ci;
    ci << "host=" << (host ? host : "127.0.0.1")
       << " port=" << (port ? port : "5432")
       << " dbname=" << (db ? db : "jkmart")
       << " user=" << (user ? user : "postgres")
       << " connect_timeout=5";
    if (pass) ci << " password=" << pass;

    pg_conn* c = PQconnectdb(ci.str().c_str());
    if (PQstatus(c) != CONNECTION_OK) {
        lastError_ = PQerrorMessage(c);
        PQfinish(c);
        return false;
    }
    conn_ = c;
    lastError_.clear();
    return true;
}

void Db::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool Db::exec(const std::string& sql) {
    if (!conn_) {
        lastError_ = "not connected";
        return false;
    }
    PGresult* res = PQexec(conn_, sql.c_str());
    if (!res) {
        lastError_ = "PQexec failed";
        return false;
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        lastError_ = PQresultErrorMessage(res);
        PQclear(res);
        return false;
    }
    PQclear(res);
    lastError_.clear();
    return true;
}

Json::Value Db::query(const std::string& sql) {
    return query(sql, {});
}

Json::Value Db::query(const std::string& sql,
                      const std::vector<std::string>& params) {
    Json::Value out(Json::arrayValue);
    if (!conn_) {
        lastError_ = "not connected";
        return out;
    }
    PGresult* res = nullptr;
    if (params.empty()) {
        res = PQexec(conn_, sql.c_str());
    } else {
        std::vector<const char*> values(params.size());
        std::vector<int> lengths(params.size());
        for (size_t i = 0; i < params.size(); ++i) {
            values[i] = params[i].c_str();
            lengths[i] = static_cast<int>(params[i].size());
        }
        res = PQexecParams(conn_, sql.c_str(), static_cast<int>(params.size()),
                           nullptr, values.data(), lengths.data(), nullptr, 0);
    }
    if (!res) {
        lastError_ = "PQexec failed";
        return out;
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        lastError_ = PQresultErrorMessage(res);
        PQclear(res);
        return out;
    }
    int rows = PQntuples(res);
    int cols = PQnfields(res);
    for (int r = 0; r < rows; ++r) {
        Json::Value row(Json::objectValue);
        for (int c = 0; c < cols; ++c) {
            if (PQgetisnull(res, r, c)) {
                row[PQfname(res, c)] = Json::Value();
            } else {
                row[PQfname(res, c)] = Json::Value(PQgetvalue(res, r, c));
            }
        }
        out.append(row);
    }
    PQclear(res);
    lastError_.clear();
    return out;
}

}