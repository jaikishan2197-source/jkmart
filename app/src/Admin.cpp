#include "Admin.h"

#include "Db.h"

#include <vector>

namespace jkmart {

bool adminUsersFiltered(const std::string& role,
                        Json::Value& out,
                        std::string& err) {
    std::string sql =
        "SELECT id, name, email, role, is_active FROM users";
    std::vector<std::string> params;
    if (role == "SELLER" || role == "BUYER" || role == "ADMIN") {
        sql += " WHERE role = $1::text";
        params.push_back(role);
    }
    sql += " ORDER BY id";
    out = Db::instance().query(sql, params);
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool setUserActive(const std::string& userId,
                   bool active,
                   std::string& err) {
    auto rows = Db::instance().query(
        "SELECT id FROM users WHERE id = $1::bigint", {userId});
    if (rows.empty()) {
        err = "user not found";
        return false;
    }
    Db::instance().query(
        "UPDATE users SET is_active = $1::boolean WHERE id = $2::bigint",
        {active ? "true" : "false", userId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool adminOrders(Json::Value& out, std::string& err) {
    out = Db::instance().query(
        "SELECT o.id, o.buyer_id, o.status, o.total, o.payment_method,"
        " to_char(o.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at,"
        " u.name AS buyer_name,"
        " (SELECT COUNT(*) FROM order_items oi WHERE oi.order_id = o.id)"
        "  AS item_count"
        " FROM orders o JOIN users u ON u.id = o.buyer_id"
        " ORDER BY o.id DESC");
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool adminReviews(Json::Value& out, std::string& err) {
    out = Db::instance().query(
        "SELECT r.id, r.product_id, r.rating, r.title, r.comment,"
        " to_char(r.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at,"
        " u.name AS user_name, p.name AS product_name"
        " FROM reviews r"
        " JOIN users u ON u.id = r.user_id"
        " JOIN products p ON p.id = r.product_id"
        " ORDER BY r.id DESC");
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

}  // namespace jkmart