#include "Reviews.h"

#include "Db.h"

#include <cctype>

namespace jkmart {

namespace {

bool hasDeliveredOrder(const std::string& productId,
                       const std::string& userId) {
    auto rows = Db::instance().query(
        "SELECT 1 FROM orders o"
        " JOIN order_items oi ON oi.order_id = o.id"
        " WHERE oi.product_id = $1::bigint"
        " AND o.buyer_id = $2::bigint"
        " AND o.status = 'DELIVERED' LIMIT 1",
        {productId, userId});
    return !rows.empty();
}

}  // namespace

bool productReviews(const std::string& productId,
                    const std::string& viewerId,
                    const std::string& viewerRole,
                    Json::Value& out,
                    std::string& err) {
    auto prod = Db::instance().query(
        "SELECT id FROM products WHERE id = $1::bigint", {productId});
    if (prod.empty()) {
        err = "product not found";
        return false;
    }

    auto agg = Db::instance().query(
        "SELECT COALESCE(AVG(rating), 0)::numeric(3,1) AS average,"
        " COUNT(*) AS count FROM reviews WHERE product_id = $1::bigint",
        {productId});

    auto rows = Db::instance().query(
        "SELECT r.id, r.rating, r.title, r.comment,"
        " to_char(r.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at,"
        " u.name AS user_name"
        " FROM reviews r JOIN users u ON u.id = r.user_id"
        " WHERE r.product_id = $1::bigint"
        " ORDER BY r.created_at DESC, r.id DESC",
        {productId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }

    Json::Value data(Json::objectValue);
    data["product_id"] = productId;
    if (!agg.empty()) {
        data["average"] = agg[0]["average"].asString();
        data["rating_count"] = agg[0]["count"].asString();
    } else {
        data["average"] = "0";
        data["rating_count"] = "0";
    }
    data["reviews"] = rows;

    bool canReview = false;
    bool alreadyReviewed = false;
    if (viewerRole == "BUYER" && !viewerId.empty()) {
        auto mine = Db::instance().query(
            "SELECT 1 FROM reviews WHERE user_id = $1::bigint"
            " AND product_id = $2::bigint LIMIT 1",
            {viewerId, productId});
        alreadyReviewed = !mine.empty();
        canReview = !alreadyReviewed && hasDeliveredOrder(productId, viewerId);
    }
    data["can_review"] = canReview;
    data["already_reviewed"] = alreadyReviewed;
    err.clear();
    out = data;
    return true;
}

bool addReview(const std::string& productId,
               const std::string& userId,
               const std::string& role,
               const std::string& rating,
               const std::string& title,
               const std::string& comment,
               Json::Value& out,
               std::string& err) {
    if (role != "BUYER") {
        err = "only buyers can review products";
        return false;
    }
    int stars = 0;
    if (rating.empty()) {
        err = "rating is required";
        return false;
    }
    for (char ch : rating) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            err = "rating must be a number from 1 to 5";
            return false;
        }
    }
    try {
        stars = std::stoi(rating);
    } catch (...) {
        err = "rating must be between 1 and 5";
        return false;
    }
    if (stars < 1 || stars > 5) {
        err = "rating must be between 1 and 5";
        return false;
    }

    auto prod = Db::instance().query(
        "SELECT id FROM products WHERE id = $1::bigint", {productId});
    if (prod.empty()) {
        err = "product not found";
        return false;
    }

    if (!hasDeliveredOrder(productId, userId)) {
        err = "you can only review a product you ordered and received";
        return false;
    }

    auto dup = Db::instance().query(
        "SELECT 1 FROM reviews WHERE user_id = $1::bigint"
        " AND product_id = $2::bigint LIMIT 1",
        {userId, productId});
    if (!dup.empty()) {
        err = "you already reviewed this product";
        return false;
    }

    auto ins = Db::instance().query(
        "INSERT INTO reviews (product_id, user_id, rating, title, comment)"
        " VALUES ($1::bigint, $2::bigint, $3::int, $4::text, $5::text)"
        " RETURNING id, rating, title, comment,"
        " to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at",
        {productId, userId, rating, title, comment});
    if (ins.empty()) {
        err = "insert failed: " + Db::instance().lastError();
        return false;
    }

    Db::instance().query(
        "UPDATE products SET"
        " rating = (SELECT COALESCE(AVG(rating), 0)::numeric(3,1)"
        "           FROM reviews WHERE product_id = $1::bigint),"
        " rating_count = (SELECT COUNT(*) FROM reviews"
        "                WHERE product_id = $1::bigint)"
        " WHERE id = $1::bigint",
        {productId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }

    auto name = Db::instance().query(
        "SELECT name FROM users WHERE id = $1::bigint", {userId});
    if (!name.empty()) {
        ins[0]["user_name"] = name[0]["name"].asString();
    }
    out = ins[0];
    err.clear();
    return true;
}

}  // namespace jkmart