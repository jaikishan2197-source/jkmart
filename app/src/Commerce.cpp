#include "Commerce.h"

#include "Db.h"

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace jkmart {

namespace {

const char* kCartItemCols =
    "c.product_id, c.quantity AS wanted,"
    " p.name, p.price, p.stock, p.image_url, p.is_active,"
    " p.seller_id, u.name AS seller_name";

const char* kWishCols =
    "w.product_id, to_char(w.added_at, 'YYYY-MM-DD HH24:MI:SS') AS added_at,"
    " p.name, p.price, p.stock, p.image_url, p.is_active,"
    " p.seller_id, u.name AS seller_name";

bool parseQty(const std::string& s, int& out) {
    if (s.empty() || s.size() > 9) return false;
    for (char ch : s) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    try {
        out = std::stoi(s);
    } catch (...) {
        return false;
    }
    return out > 0 && out <= 1000000;
}

bool productFor(const std::string& id, Json::Value& prod) {
    auto rows = Db::instance().query(
        "SELECT id, name, price, stock, is_active FROM products"
        " WHERE id = $1::bigint",
        {id});
    if (rows.empty()) return false;
    prod = rows[0];
    return true;
}

Json::Value orderDetailInternal(const std::string& orderId,
                                std::string& err) {
    auto ord = Db::instance().query(
        "SELECT o.id, o.buyer_id, o.status, o.total, o.payment_method,"
        " to_char(o.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at,"
        " u.name AS buyer_name"
        " FROM orders o JOIN users u ON u.id = o.buyer_id"
        " WHERE o.id = $1::bigint",
        {orderId});
    if (ord.empty()) {
        err = "order not found";
        return Json::Value(Json::nullValue);
    }
    Json::Value out = ord[0];
    auto items = Db::instance().query(
        "SELECT id, product_id, name, price, quantity"
        " FROM order_items WHERE order_id = $1::bigint ORDER BY id",
        {orderId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::nullValue);
    }
    out["items"] = items;
    err.clear();
    return out;
}

bool ownsOrder(const std::string& orderId, const std::string& userId) {
    auto rows = Db::instance().query(
        "SELECT 1 FROM order_items oi JOIN products p ON p.id = oi.product_id"
        " WHERE oi.order_id = $1::bigint AND p.seller_id = $2::bigint"
        " LIMIT 1",
        {orderId, userId});
    return !rows.empty();
}

}  // namespace

/* ---------------- Cart ---------------- */

Json::Value getCart(const std::string& buyerId, std::string& err) {
    Json::Value items = Db::instance().query(
        std::string("SELECT ") + kCartItemCols +
            " FROM cart_items c"
            " JOIN products p ON p.id = c.product_id"
            " JOIN users u ON u.id = p.seller_id"
            " WHERE c.buyer_id = $1::bigint"
            " ORDER BY c.added_at DESC, c.product_id",
        {buyerId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::nullValue);
    }
    double total = 0.0;
    int count = 0;
    for (auto& row : items) {
        int qty = std::stoi(row["wanted"].asString());
        double price = std::stod(row["price"].asString());
        count += qty;
        char lbuf[32];
        std::snprintf(lbuf, sizeof lbuf, "%.2f", price * qty);
        row["line_total"] = lbuf;
        total += price * qty;
    }
    Json::Value out(Json::objectValue);
    out["items"] = items;
    out["count"] = count;
    out["item_count"] = static_cast<int>(items.size());
    char tbuf[32];
    std::snprintf(tbuf, sizeof tbuf, "%.2f", total);
    out["subtotal"] = tbuf;
    out["total"] = tbuf;
    err.clear();
    return out;
}

bool addToCart(const std::string& buyerId,
               const std::string& productId,
               const std::string& quantity,
               std::string& err) {
    int qty;
    if (!parseQty(quantity, qty)) {
        err = "invalid quantity";
        return false;
    }
    Json::Value prod;
    if (!productFor(productId, prod) || prod["is_active"].asString() != "t") {
        err = "product not found or inactive";
        return false;
    }
    int stock = std::stoi(prod["stock"].asString());
    if (stock <= 0) {
        err = "product is out of stock";
        return false;
    }
    auto cur = Db::instance().query(
        "SELECT quantity FROM cart_items"
        " WHERE buyer_id = $1::bigint AND product_id = $2::bigint",
        {buyerId, productId});
    int current = cur.empty() ? 0 : std::stoi(cur[0]["quantity"].asString());
    if (current >= stock) {
        err = "only " + std::to_string(stock) +
              " available in stock";
        return false;
    }
    int newQty = std::min(current + qty, stock);
    Db::instance().query(
        "INSERT INTO cart_items (buyer_id, product_id, quantity)"
        " VALUES ($1::bigint, $2::bigint, $3::int)"
        " ON CONFLICT (buyer_id, product_id) DO UPDATE"
        " SET quantity = EXCLUDED.quantity",
        {buyerId, productId, std::to_string(newQty)});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool setCartQuantity(const std::string& buyerId,
                     const std::string& productId,
                     const std::string& quantity,
                     std::string& err) {
    int qty;
    if (!parseQty(quantity, qty)) {
        if (quantity == "0") {
            return removeCartItem(buyerId, productId, err);
        }
        err = "invalid quantity";
        return false;
    }
    Json::Value prod;
    if (!productFor(productId, prod) || prod["is_active"].asString() != "t") {
        err = "product not found or inactive";
        return false;
    }
    int stock = std::stoi(prod["stock"].asString());
    if (qty > stock) {
        err = "only " + std::to_string(stock) + " available in stock";
        return false;
    }
    Db::instance().query(
        "INSERT INTO cart_items (buyer_id, product_id, quantity)"
        " VALUES ($1::bigint, $2::bigint, $3::int)"
        " ON CONFLICT (buyer_id, product_id) DO UPDATE"
        " SET quantity = EXCLUDED.quantity",
        {buyerId, productId, std::to_string(qty)});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool removeCartItem(const std::string& buyerId,
                    const std::string& productId,
                    std::string& err) {
    Db::instance().query(
        "DELETE FROM cart_items"
        " WHERE buyer_id = $1::bigint AND product_id = $2::bigint",
        {buyerId, productId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool clearCart(const std::string& buyerId, std::string& err) {
    Db::instance().query(
        "DELETE FROM cart_items WHERE buyer_id = $1::bigint", {buyerId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

/* ---------------- Wishlist ---------------- */

Json::Value getWishlist(const std::string& buyerId, std::string& err) {
    Json::Value rows = Db::instance().query(
        std::string("SELECT ") + kWishCols +
            " FROM wishlist_items w"
            " JOIN products p ON p.id = w.product_id"
            " JOIN users u ON u.id = p.seller_id"
            " WHERE w.buyer_id = $1::bigint"
            " ORDER BY w.added_at DESC, w.product_id",
        {buyerId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::nullValue);
    }
    err.clear();
    return rows;
}

bool addWishlist(const std::string& buyerId,
                 const std::string& productId,
                 std::string& err) {
    Json::Value prod;
    if (!productFor(productId, prod) || prod["is_active"].asString() != "t") {
        err = "product not found or inactive";
        return false;
    }
    Db::instance().query(
        "INSERT INTO wishlist_items (buyer_id, product_id)"
        " VALUES ($1::bigint, $2::bigint) ON CONFLICT DO NOTHING",
        {buyerId, productId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

bool removeWishlist(const std::string& buyerId,
                    const std::string& productId,
                    std::string& err) {
    Db::instance().query(
        "DELETE FROM wishlist_items"
        " WHERE buyer_id = $1::bigint AND product_id = $2::bigint",
        {buyerId, productId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

/* ---------------- Orders ---------------- */

Json::Value placeOrder(const std::string& buyerId,
                       const std::string& paymentMethod,
                       std::string& err) {
    std::string method = paymentMethod.empty() ? "DEMO" : paymentMethod;
    Db& db = Db::instance();

    if (!db.exec("BEGIN")) {
        err = "transaction failed: " + db.lastError();
        return Json::Value(Json::nullValue);
    }

    auto cart = db.query(
        "SELECT product_id, quantity FROM cart_items"
        " WHERE buyer_id = $1::bigint ORDER BY product_id",
        {buyerId});
    if (db.lastErrorSet() || cart.empty()) {
        db.exec("ROLLBACK");
        err = "cart is empty";
        return Json::Value(Json::nullValue);
    }

    std::vector<std::string> purchaseIds, purchasePrices, purchaseQtys;
    std::vector<std::string> purchaseNames;
    double total = 0.0;
    for (const auto& row : cart) {
        std::string pid = row["product_id"].asString();
        int qty = std::stoi(row["quantity"].asString());
        auto prod = db.query(
            "SELECT id, name, price, stock, is_active FROM products"
            " WHERE id = $1::bigint FOR UPDATE",
            {pid});
        if (db.lastErrorSet() || prod.empty()) {
            db.exec("ROLLBACK");
            err = "product no longer available";
            return Json::Value(Json::nullValue);
        }
        std::string name = prod[0]["name"].asString();
        if (prod[0]["is_active"].asString() != "t") {
            db.exec("ROLLBACK");
            err = "product '" + name + "' is not available";
            return Json::Value(Json::nullValue);
        }
        int stock = std::stoi(prod[0]["stock"].asString());
        if (qty > stock) {
            db.exec("ROLLBACK");
            err = "insufficient stock for '" + name + "' (only " +
                  std::to_string(stock) + " left)";
            return Json::Value(Json::nullValue);
        }
        std::string price = prod[0]["price"].asString();
        total += std::stod(price) * qty;
        purchaseIds.push_back(pid);
        purchaseNames.push_back(name);
        purchasePrices.push_back(price);
        purchaseQtys.push_back(std::to_string(qty));
    }

    char tbuf[32];
    std::snprintf(tbuf, sizeof tbuf, "%.2f", total);

    auto ins = db.query(
        "INSERT INTO orders (buyer_id, status, total, payment_method)"
        " VALUES ($1::bigint, 'PLACED', $2::numeric, $3::text)"
        " RETURNING id",
        {buyerId, tbuf, method});
    if (ins.empty()) {
        db.exec("ROLLBACK");
        err = "order insert failed: " + db.lastError();
        return Json::Value(Json::nullValue);
    }
    std::string orderId = ins[0]["id"].asString();

    for (size_t i = 0; i < purchaseIds.size(); ++i) {
        db.query(
            "INSERT INTO order_items (order_id, product_id, name, price,"
            " quantity) VALUES ($1::bigint, $2::bigint, $3::text,"
            " $4::numeric, $5::int)",
            {orderId, purchaseIds[i], purchaseNames[i], purchasePrices[i],
             purchaseQtys[i]});
        if (db.lastErrorSet()) {
            db.exec("ROLLBACK");
            err = db.lastError();
            return Json::Value(Json::nullValue);
        }
        db.query(
            "UPDATE products SET stock = stock - $1::int WHERE id = $2::bigint",
            {purchaseQtys[i], purchaseIds[i]});
        if (db.lastErrorSet()) {
            db.exec("ROLLBACK");
            err = db.lastError();
            return Json::Value(Json::nullValue);
        }
    }

    db.query("DELETE FROM cart_items WHERE buyer_id = $1::bigint", {buyerId});
    if (db.lastErrorSet()) {
        db.exec("ROLLBACK");
        err = db.lastError();
        return Json::Value(Json::nullValue);
    }

    if (!db.exec("COMMIT")) {
        db.exec("ROLLBACK");
        err = "commit failed: " + db.lastError();
        return Json::Value(Json::nullValue);
    }

    return orderDetailInternal(orderId, err);
}

Json::Value orderHistory(const std::string& buyerId, std::string& err) {
    Json::Value rows = Db::instance().query(
        "SELECT id, status, total, payment_method,"
        " to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at"
        " FROM orders WHERE buyer_id = $1::bigint ORDER BY id DESC",
        {buyerId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::arrayValue);
    }
    err.clear();
    return rows;
}

bool orderDetail(const std::string& orderId,
                 const std::string& userId,
                 bool isAdmin,
                 Json::Value& out,
                 std::string& err) {
    if (!isAdmin) {
        auto ord = Db::instance().query(
            "SELECT buyer_id FROM orders WHERE id = $1::bigint", {orderId});
        if (ord.empty()) {
            err = "order not found";
            return false;
        }
        bool owner = ord[0]["buyer_id"].asString() == userId;
        if (!owner && !ownsOrder(orderId, userId)) {
            err = "order not found";
            return false;
        }
    }
    out = orderDetailInternal(orderId, err);
    return !err.empty() ? false : true;
}

Json::Value sellerOrders(const std::string& sellerId, std::string& err) {
    Json::Value rows = Db::instance().query(
        "SELECT DISTINCT o.id, o.buyer_id, o.status, o.total, o.payment_method,"
        " to_char(o.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at,"
        " (SELECT u.name FROM users u WHERE u.id = o.buyer_id) AS buyer_name,"
        " (SELECT COUNT(*) FROM order_items oi WHERE oi.order_id = o.id)"
        "  AS item_count"
        " FROM orders o"
        " JOIN order_items oi ON oi.order_id = o.id"
        " JOIN products p ON p.id = oi.product_id"
        " WHERE p.seller_id = $1::bigint"
        " ORDER BY o.id DESC",
        {sellerId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return Json::Value(Json::arrayValue);
    }
    err.clear();
    return rows;
}

bool updateOrderStatus(const std::string& orderId,
                       const std::string& userId,
                       bool isAdmin,
                       const std::string& status,
                       std::string& err) {
    static const std::set<std::string> kStatuses = {
        "PLACED",    "CONFIRMED", "PROCESSING",
        "SHIPPED",   "DELIVERED", "CANCELLED"};
    if (kStatuses.find(status) == kStatuses.end()) {
        err = "invalid status";
        return false;
    }
    if (!isAdmin && !ownsOrder(orderId, userId)) {
        err = "permission denied";
        return false;
    }
    auto ord = Db::instance().query(
        "SELECT id FROM orders WHERE id = $1::bigint", {orderId});
    if (ord.empty()) {
        err = "order not found";
        return false;
    }
    Db::instance().query(
        "UPDATE orders SET status = $1::text WHERE id = $2::bigint",
        {status, orderId});
    if (Db::instance().lastErrorSet()) {
        err = Db::instance().lastError();
        return false;
    }
    err.clear();
    return true;
}

}  // namespace jkmart