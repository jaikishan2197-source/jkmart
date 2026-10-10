#include <drogon/drogon.h>
#include "Db.h"

using namespace drogon;

int main() {
    auto& db = jkmart::Db::instance();

    if (!db.connect()) {
        LOG_FATAL << "PostgreSQL connection failed: " << db.lastError();
        return 1;
    }
    LOG_INFO << "Connected to PostgreSQL via raw libpq.";

    app().registerHandler(
        "/api/health",
        [](const HttpRequestPtr&,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value resp(Json::objectValue);
            auto rows = jkmart::Db::instance().query(
                "SELECT current_database() AS database,"
                " now() AS server_time,"
                " 42 AS answer");
            if (rows.isArray() && !rows.empty()) {
                resp["status"] = "ok";
                resp["db"] = rows[0];
                callback(HttpResponse::newHttpJsonResponse(resp));
                return;
            }
            resp["status"] = "error";
            resp["db_error"] = jkmart::Db::instance().lastError();
            auto r = HttpResponse::newHttpJsonResponse(resp);
            r->setStatusCode(k500InternalServerError);
            callback(r);
        },
        {Get, Post});

    app().addListener("0.0.0.0", 8080);
    app().setLogLevel(trantor::Logger::kInfo);
    app().run();
    return 0;
}