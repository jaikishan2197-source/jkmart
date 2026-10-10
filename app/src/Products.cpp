#include "Products.h"

#include "Db.h"

#include <cctype>
#include <sstream>

namespace jkmart {

namespace {

const char* kProductCols =
    "p.id, p.name, p.description, p.price, p.stock, p.category, p.image_url,"
    " p.rating, p.rating_count, p.is_active,"
    " to_char(p.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at,"
    " u.id AS seller_id, u.name AS seller_name";

bool parseDecimal(const std::string& s, std::string& out) {
    std::string t;
    bool sawDot = false;
    size_t fracLen = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '.') {
            if (sawDot) return false;
            sawDot = true;
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        } else if (sawDot) {
            ++fracLen;
        }
        t.push_back(c);
    }
    if (t.empty() || t == "." || t.size() > 12 || fracLen > 2) return false;
    try {
        double v = std::stod(t);
        if (v < 0 || v > 9999999.99) return false;
    } catch (...) {
        return false;
    }
    out = t;
    return true;
}

bool parseStock(const std::string& s, int& out) {
    if (s.empty() || s.size() > 9 || (s.size() > 1 && s[0] == '-')) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        out = std::stoi(s);
    } catch (...) {
        return false;
    }
    if (out < 0 || out > 1000000) return false;
    return true;
}

bool validImageUrl(const std::string& s) {
    if (s.empty()) return true;
    if (s.size() > 2048) return false;
    if (s.compare(0, 7, "http://") != 0 &&
        s.compare(0, 8, "https://") != 0) {
        return false;
    }
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20) return false;
    }
    return true;
}

Json::Value joinedRow(const std::string& id) {
    auto rows = Db::instance().query(
        std::string("SELECT ") + kProductCols +
            " FROM products p JOIN users u ON u.id = p.seller_id"
            " WHERE p.id = $1::bigint",
        {id});
    if (!rows.empty()) return rows[0];
    return Json::Value(Json::nullValue);
}

}  // namespace

Json::Value productsList(const std::string& query,
                         const std::string& category,
                         const std::string& sort,
                         int limit,
                         std::string& err) {
    std::string sql = std::string("SELECT ") + kProductCols +
                      " FROM products p"
                      " JOIN users u ON u.id = p.seller_id"
                      " WHERE p.is_active = TRUE";
    std::vector<std::string> params;

    if (!query.empty()) {
        sql += " AND (p.name ILIKE '%'||$" +
               std::to_string(params.size() + 1) +
               "||'%' OR p.category ILIKE '%'||$" +
               std::to_string(params.size() + 1) +
               "||'%' OR p.description ILIKE '%'||$" +
               std::to_string(params.size() + 1) + "||'%')";
        params.push_back(query);
    }
    if (!category.empty()) {
        sql += " AND p.category = $" + std::to_string(params.size() + 1);
        params.push_back(category);
    }
    if (sort == "latest") {
        sql += " ORDER BY p.created_at DESC, p.id DESC";
    } else {
        sql += " ORDER BY p.rating DESC, p.rating_count DESC, p.id DESC";
    }
    if (limit > 0) {
        sql += " LIMIT $" + std::to_string(params.size() + 1);
        params.push_back(std::to_string(limit));
    }

    Json::Value out = Db::instance().query(sql, params);
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::arrayValue);
    }
    err.clear();
    return out;
}

Json::Value productById(const std::string& id, std::string& err) {
    auto rows = Db::instance().query(
        std::string("SELECT ") + kProductCols +
            " FROM products p JOIN users u ON u.id = p.seller_id"
            " WHERE p.id = $1::bigint AND p.is_active = TRUE",
        {id});
    if (rows.empty()) {
        err = "product not found";
        return Json::Value(Json::nullValue);
    }
    err.clear();
    return rows[0];
}

Json::Value sellerProducts(const std::string& sellerId, std::string& err) {
    Json::Value out = Db::instance().query(
        std::string("SELECT ") + kProductCols +
            " FROM products p JOIN users u ON u.id = p.seller_id"
            " WHERE p.seller_id = $1::bigint"
            " ORDER BY p.created_at DESC, p.id DESC",
        {sellerId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::arrayValue);
    }
    err.clear();
    return out;
}

Json::Value sellerStats(const std::string& sellerId, std::string& err) {
    auto rows = Db::instance().query(
        "SELECT COUNT(*) AS total,"
        " COUNT(*) FILTER (WHERE is_active) AS active,"
        " COUNT(*) FILTER (WHERE NOT is_active) AS inactive,"
        " COUNT(*) FILTER (WHERE stock = 0) AS out_of_stock,"
        " COALESCE(SUM(stock), 0) AS total_stock"
        " FROM products WHERE seller_id = $1::bigint",
        {sellerId});
    if (rows.empty()) {
        err = "stats query failed: " + Db::instance().lastError();
        return Json::Value(Json::nullValue);
    }
    err.clear();
    return rows[0];
}

bool adminUsers(Json::Value& out, std::string& err) {
    out = Db::instance().query(
        "SELECT id, name, email, role FROM users ORDER BY id");
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool adminProducts(Json::Value& out, std::string& err) {
    out = Db::instance().query(
        std::string("SELECT ") + kProductCols +
            " FROM products p JOIN users u ON u.id = p.seller_id"
            " ORDER BY p.id DESC");
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool categories(Json::Value& out, std::string& err) {
    out = Db::instance().query(
        "SELECT DISTINCT category FROM products WHERE is_active = TRUE"
        " ORDER BY category");
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool createProduct(const std::string& sellerId,
                   const Json::Value& body,
                   Json::Value& product,
                   std::string& err) {
    std::string name = body.get("name", "").asString();
    if (name.empty()) {
        err = "name is required";
        return false;
    }
    std::string price;
    if (!parseDecimal(body.get("price", "0").asString(), price)) {
        err = "price must be a number";
        return false;
    }
    if (price.find('.') == std::string::npos) price += ".00";
    int stock = 0;
    std::string stockStr = body.get("stock", "0").asString();
    if (!parseStock(stockStr, stock)) {
        err = "stock must be a non-negative integer";
        return false;
    }
    std::string category = body.get("category", "").asString();
    if (category.empty()) category = "General";
    std::string description = body.get("description", "").asString();
    std::string imageUrl = body.get("image_url", "").asString();
    if (!validImageUrl(imageUrl)) {
        err = "image_url must be an http(s) URL";
        return false;
    }

    auto rows = Db::instance().query(
        "INSERT INTO products (seller_id, name, description, price, stock,"
        " category, image_url)"
        " VALUES ($1::bigint, $2::text, $3::text, $4::numeric,"
        " $5::int, $6::text, $7::text)"
        " RETURNING id, name, description, price, stock, category, image_url,"
        " rating, rating_count, is_active,"
        " to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at",
        {sellerId, name, description, price, std::to_string(stock), category,
         imageUrl});
    if (rows.empty()) {
        err = "insert failed: " + Db::instance().lastError();
        return false;
    }
    product = joinedRow(rows[0]["id"].asString());
    err.clear();
    return true;
}

bool updateProduct(const std::string& id,
                   const std::string& userId,
                   bool isAdmin,
                   const Json::Value& body,
                   Json::Value& product,
                   std::string& err) {
    auto current = Db::instance().query(
        "SELECT seller_id, name, description, price, stock, category,"
        " image_url, is_active FROM products WHERE id = $1::bigint",
        {id});
    if (current.empty()) {
        err = "product not found";
        return false;
    }
    if (!isAdmin && current[0]["seller_id"].asString() != userId) {
        err = "permission denied";
        return false;
    }

    std::string name = body.isMember("name") ? body["name"].asString()
                                             : current[0]["name"].asString();
    if (name.empty()) {
        err = "name is required";
        return false;
    }
    std::string price =
        body.isMember("price") ? body["price"].asString()
                               : current[0]["price"].asString();
    std::string p = price;
    if (!parseDecimal(p, p)) {
        err = "price must be a number";
        return false;
    }
    if (p.find('.') == std::string::npos) p += ".00";
    int stock = 0;
    std::string stockStr = body.isMember("stock")
                               ? body["stock"].asString()
                               : current[0]["stock"].asString();
    if (!parseStock(stockStr, stock)) {
        err = "stock must be a non-negative integer";
        return false;
    }
    std::string category =
        body.isMember("category") ? body["category"].asString()
                                  : current[0]["category"].asString();
    if (category.empty()) category = "General";
    std::string description =
        body.isMember("description") ? body["description"].asString()
                                     : current[0]["description"].asString();
    std::string imageUrl = body.isMember("image_url")
                               ? body["image_url"].asString()
                               : current[0]["image_url"].asString();
    if (!validImageUrl(imageUrl)) {
        err = "image_url must be an http(s) URL";
        return false;
    }
    bool isActive = current[0]["is_active"].asString() == "t";
    if (body.isMember("is_active")) {
        isActive = body["is_active"].asBool();
    }

    Db::instance().query(
        "UPDATE products SET name = $1::text, description = $2::text,"
        " price = $3::numeric, stock = $4::int, category = $5::text,"
        " image_url = $6::text, is_active = $7::boolean"
        " WHERE id = $8::bigint",
        {name, description, p, std::to_string(stock), category, imageUrl,
         isActive ? "true" : "false", id});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    product = joinedRow(id);
    err.clear();
    return true;
}

bool setProductActive(const std::string& id,
                      const std::string& userId,
                      bool isAdmin,
                      bool active,
                      std::string& err) {
    auto current = Db::instance().query(
        "SELECT seller_id FROM products WHERE id = $1::bigint", {id});
    if (current.empty()) {
        err = "product not found";
        return false;
    }
    if (!isAdmin && current[0]["seller_id"].asString() != userId) {
        err = "permission denied";
        return false;
    }
    Db::instance().query(
        "UPDATE products SET is_active = $1::boolean WHERE id = $2::bigint",
        {active ? "true" : "false", id});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

}  // namespace jkmart