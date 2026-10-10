#include <cstdlib>
#include <string>
#include <drogon/drogon.h>

#include "Auth.h"
#include "Db.h"
#include "Handlers.h"

using namespace drogon;

namespace {

Json::Value userJson(const jkmart::User& u) {
    Json::Value v(Json::objectValue);
    v["id"] = u.id;
    v["name"] = u.name;
    v["email"] = u.email;
    v["role"] = u.role;
    v["is_active"] = u.isActive;
    return v;
}

Json::Value errorJson(const std::string& msg) {
    Json::Value v(Json::objectValue);
    v["error"] = msg;
    return v;
}

std::string bearerToken(const HttpRequestPtr& req) {
    const std::string& auth = req->getHeader("Authorization");
    if (auth.compare(0, 7, "Bearer ") == 0) {
        return auth.substr(7);
    }
    return "";
}

}  // namespace

int main() {
    auto& db = jkmart::Db::instance();

    if (!db.connect()) {
        LOG_FATAL << "PostgreSQL connection failed: " << db.lastError();
        return 1;
    }
    LOG_INFO << "Connected to PostgreSQL via raw libpq.";

    db.query(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  email TEXT NOT NULL UNIQUE,"
        "  password_hash TEXT NOT NULL,"
        "  role TEXT NOT NULL DEFAULT 'BUYER',"
        "  CHECK (role IN ('BUYER','SELLER','ADMIN')),"
        "  is_active BOOLEAN NOT NULL DEFAULT TRUE"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create users table: " << db.lastError();
        return 1;
    }
    db.query(
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS"
        " is_active BOOLEAN NOT NULL DEFAULT TRUE");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to migrate users table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "users table ready.";

    db.query(
        "CREATE TABLE IF NOT EXISTS products ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  seller_id BIGINT NOT NULL REFERENCES users(id),"
        "  name TEXT NOT NULL,"
        "  description TEXT NOT NULL DEFAULT '',"
        "  price NUMERIC(10,2) NOT NULL DEFAULT 0,"
        "  stock INT NOT NULL DEFAULT 0,"
        "  category TEXT NOT NULL DEFAULT 'General',"
        "  image_url TEXT NOT NULL DEFAULT '',"
        "  rating NUMERIC(3,1) NOT NULL DEFAULT 0,"
        "  rating_count INT NOT NULL DEFAULT 0,"
        "  is_active BOOLEAN NOT NULL DEFAULT TRUE,"
        "  created_at TIMESTAMPTZ NOT NULL DEFAULT now()"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create products table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "products table ready.";

    db.query(
        "CREATE TABLE IF NOT EXISTS cart_items ("
        "  buyer_id BIGINT NOT NULL REFERENCES users(id),"
        "  product_id BIGINT NOT NULL REFERENCES products(id),"
        "  quantity INT NOT NULL CHECK (quantity > 0),"
        "  added_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
        "  PRIMARY KEY (buyer_id, product_id)"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create cart_items table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "cart_items table ready.";

    db.query(
        "CREATE TABLE IF NOT EXISTS wishlist_items ("
        "  buyer_id BIGINT NOT NULL REFERENCES users(id),"
        "  product_id BIGINT NOT NULL REFERENCES products(id),"
        "  added_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
        "  PRIMARY KEY (buyer_id, product_id)"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create wishlist_items table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "wishlist_items table ready.";

    db.query(
        "CREATE TABLE IF NOT EXISTS orders ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  buyer_id BIGINT NOT NULL REFERENCES users(id),"
        "  status TEXT NOT NULL DEFAULT 'PLACED'"
        "  CHECK (status IN ('PLACED','CONFIRMED','PROCESSING',"
        "                     'SHIPPED','DELIVERED','CANCELLED')),"
        "  total NUMERIC(12,2) NOT NULL DEFAULT 0,"
        "  payment_method TEXT NOT NULL DEFAULT 'DEMO',"
        "  created_at TIMESTAMPTZ NOT NULL DEFAULT now()"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create orders table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "orders table ready.";

    db.query(
        "CREATE TABLE IF NOT EXISTS order_items ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  order_id BIGINT NOT NULL REFERENCES orders(id),"
        "  product_id BIGINT NOT NULL REFERENCES products(id),"
        "  name TEXT NOT NULL,"
        "  price NUMERIC(10,2) NOT NULL,"
        "  quantity INT NOT NULL,"
        "  UNIQUE (order_id, product_id)"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create order_items table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "order_items table ready.";

    db.query(
        "CREATE TABLE IF NOT EXISTS reviews ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  product_id BIGINT NOT NULL REFERENCES products(id),"
        "  user_id BIGINT NOT NULL REFERENCES users(id),"
        "  rating INT NOT NULL CHECK (rating BETWEEN 1 AND 5),"
        "  title TEXT NOT NULL DEFAULT '',"
        "  comment TEXT NOT NULL DEFAULT '',"
        "  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
        "  UNIQUE (user_id, product_id)"
        ")");
    if (db.lastErrorSet()) {
        LOG_FATAL << "Failed to create reviews table: " << db.lastError();
        return 1;
    }
    LOG_INFO << "reviews table ready.";

    jkmart::registerProductHandlers();
    jkmart::registerAdminHandlers();
    jkmart::registerCommerceHandlers();
    jkmart::registerReviewHandlers();
    jkmart::registerAssistantHandlers();

    app().registerHandler(
        "/api/health",
        [](const HttpRequestPtr&,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value resp(Json::objectValue);
            auto rows = jkmart::Db::instance().query(
                "SELECT current_database() AS database,"
                " now() AS server_time,"
                " 42 AS answer");
            if (!rows.empty()) {
                resp["status"] = "ok";
                resp["db"] = rows[0];
                callback(HttpResponse::newHttpJsonResponse(resp));
                return;
            }
            resp["status"] = "error";
            resp["db_error"] = "database unavailable";
            auto r = HttpResponse::newHttpJsonResponse(resp);
            r->setStatusCode(k500InternalServerError);
            callback(r);
        },
        {Get, Post});

    app().registerHandler(
        "/api/auth/register",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;

            std::string name = body.get("name", "").asString();
            std::string email = body.get("email", "").asString();
            std::string password = body.get("password", "").asString();
            std::string role = body.get("role", "").asString();

            auto bad = [&](const std::string& msg) {
                auto r = HttpResponse::newHttpJsonResponse(errorJson(msg));
                r->setStatusCode(k400BadRequest);
                callback(r);
            };
            if (name.empty() || email.empty() || password.empty()) {
                bad("name, email and password are required");
                return;
            }
            if (email.find('@') == std::string::npos) {
                bad("invalid email");
                return;
            }
            if (password.size() < 6) {
                bad("password must be at least 6 characters");
                return;
            }

            jkmart::User user;
            std::string err;
            if (!jkmart::authRegister(name, email, password, role, user, err)) {
                auto r = HttpResponse::newHttpJsonResponse(errorJson(err));
                if (err == "email already registered") {
                    r->setStatusCode(k409Conflict);
                } else if (err.find("invalid role") != std::string::npos) {
                    r->setStatusCode(k400BadRequest);
                } else if (err == "invalid email" ||
                           err == "password must be at least 6 characters" ||
                           err == "name, email and password are required") {
                    r->setStatusCode(k400BadRequest);
                } else {
                    auto masked = HttpResponse::newHttpJsonResponse(
                        errorJson("internal server error"));
                    masked->setStatusCode(k500InternalServerError);
                    callback(masked);
                    return;
                }
                callback(r);
                return;
            }

            Json::Value resp(Json::objectValue);
            resp["user"] = userJson(user);
            resp["token"] = jkmart::createSession(user);
            auto r = HttpResponse::newHttpJsonResponse(resp);
            r->setStatusCode(k201Created);
            callback(r);
        },
        {Post});

    app().registerHandler(
        "/api/auth/login",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;

            std::string email = body.get("email", "").asString();
            std::string password = body.get("password", "").asString();

            jkmart::User user;
            std::string err;
            if (!jkmart::authLogin(email, password, user, err)) {
                auto r = HttpResponse::newHttpJsonResponse(errorJson(err));
                r->setStatusCode(k401Unauthorized);
                callback(r);
                return;
            }

            Json::Value resp(Json::objectValue);
            resp["user"] = userJson(user);
            resp["token"] = jkmart::createSession(user);
            callback(HttpResponse::newHttpJsonResponse(resp));
        },
        {Post});

    app().registerHandler(
        "/api/auth/logout",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            std::string token = bearerToken(req);
            jkmart::User user;
            if (token.empty() || !jkmart::userByToken(token, user)) {
                auto r = HttpResponse::newHttpJsonResponse(
                    errorJson("not authenticated"));
                r->setStatusCode(k401Unauthorized);
                callback(r);
                return;
            }
            jkmart::destroySession(token);
            Json::Value resp(Json::objectValue);
            resp["status"] = "logged out";
            callback(HttpResponse::newHttpJsonResponse(resp));
        },
        {Post});

    app().registerHandler(
        "/api/auth/me",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            std::string token = bearerToken(req);
            jkmart::User user;
            if (token.empty() || !jkmart::userByToken(token, user)) {
                auto r = HttpResponse::newHttpJsonResponse(
                    errorJson("not authenticated"));
                r->setStatusCode(k401Unauthorized);
                callback(r);
                return;
            }
            Json::Value resp(Json::objectValue);
            resp["user"] = userJson(user);
            callback(HttpResponse::newHttpJsonResponse(resp));
        },
        {Get});

    const char* portEnv = std::getenv("PORT");
    const uint16_t port = portEnv ? static_cast<uint16_t>(std::stoi(portEnv)) : 8080;
    app().addListener("0.0.0.0", port);
    app().setDocumentRoot("/app/public");
    app().setLogLevel(trantor::Logger::kInfo);
    app().run();
    return 0;
}
