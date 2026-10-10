#include "Handlers.h"

#include <drogon/drogon.h>

#include <json/json.h>

#include "Admin.h"
#include "Assistant.h"
#include "Auth.h"
#include "Commerce.h"
#include "Products.h"
#include "Reviews.h"

#include <drogon/HttpClient.h>

#include <cctype>

using namespace drogon;

namespace jkmart {
namespace {

Json::Value errorJson(const std::string& msg) {
    Json::Value v(Json::objectValue);
    v["error"] = msg;
    return v;
}

void replyJson(const std::function<void(const HttpResponsePtr&)>& cb,
               Json::Value v,
               HttpStatusCode code = k200OK) {
    auto r = HttpResponse::newHttpJsonResponse(std::move(v));
    r->setStatusCode(code);
    cb(r);
}

bool validId(const std::string& s) {
    if (s.empty() || s.size() > 12) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

void replyError(const std::function<void(const HttpResponsePtr&)>& cb,
                const std::string& err,
                HttpStatusCode code = k500InternalServerError) {
    if (code == k500InternalServerError) {
        LOG_ERROR << "internal error: " << err;
        replyJson(cb, errorJson("internal server error"),
                  k500InternalServerError);
        return;
    }
    replyJson(cb, errorJson(err), code);
}

bool authenticated(const HttpRequestPtr& req, User& user) {
    const std::string& auth = req->getHeader("Authorization");
    std::string token;
    if (auth.compare(0, 7, "Bearer ") == 0) token = auth.substr(7);
    return !token.empty() && userByToken(token, user);
}

void replyCart(const std::function<void(const HttpResponsePtr&)>& cb,
               const std::string& userId) {
    std::string err;
    Json::Value cart = getCart(userId, err);
    if (!err.empty()) {
        replyError(cb, err);
        return;
    }
    replyJson(cb, cart);
}

bool commerceSellerOnly(const HttpRequestPtr& req,
                        const std::function<void(const HttpResponsePtr&)>& cb,
                        User& user) {
    if (!authenticated(req, user)) {
        replyJson(cb, errorJson("not authenticated"), k401Unauthorized);
        return false;
    }
    if (user.role != "SELLER" && user.role != "ADMIN") {
        replyJson(cb, errorJson("seller access required"), k403Forbidden);
        return false;
    }
    return true;
}

bool commerceAdminOnly(const HttpRequestPtr& req,
                       const std::function<void(const HttpResponsePtr&)>& cb,
                       User& user) {
    if (!authenticated(req, user)) {
        replyJson(cb, errorJson("not authenticated"), k401Unauthorized);
        return false;
    }
    if (user.role != "ADMIN") {
        replyJson(cb, errorJson("admin access required"), k403Forbidden);
        return false;
    }
    return true;
}

}  // namespace

void registerProductHandlers() {
    app().registerHandler(
        "/api/products",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            std::string q = req->getParameter("q");
            std::string category = req->getParameter("category");
            std::string sort = req->getParameter("sort");
            std::string limitStr = req->getParameter("limit");
            int limit = 0;
            if (!limitStr.empty()) {
                try {
                    limit = std::stoi(limitStr);
                } catch (...) {
                    limit = 0;
                }
            }
            std::string err;
            Json::Value products =
                productsList(q, category, sort, limit, err);
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, products);
        },
        {Get});

    app().registerHandler(
        "/api/products/{1}",
        [](const HttpRequestPtr&,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            std::string err;
            Json::Value product = productById(id, err);
            if (!err.empty()) {
                replyError(callback, err,
                           err == "product not found" ? k404NotFound
                                                      : k500InternalServerError);
                return;
            }
            replyJson(callback, product);
        },
        {Get});

    app().registerHandler(
        "/api/categories",
        [](const HttpRequestPtr&,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value cats;
            std::string err;
            if (!categories(cats, err)) {
                replyError(callback, err);
                return;
            }
            Json::Value out(Json::arrayValue);
            for (const auto& c : cats) {
                out.append(c["category"].asString());
            }
            replyJson(callback, out);
        },
        {Get});

    app().registerHandler(
        "/api/seller/products",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (user.role != "SELLER" && user.role != "ADMIN") {
                replyJson(callback, errorJson("seller access required"),
                          k403Forbidden);
                return;
            }
            std::string err;
            Json::Value products = sellerProducts(user.id, err);
            Json::Value stats(Json::nullValue);
            if (err.empty()) {
                stats = sellerStats(user.id, err);
            }
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            Json::Value out(Json::objectValue);
            out["products"] = products;
            out["stats"] = stats;
            replyJson(callback, out);
        },
        {Get});

    app().registerHandler(
        "/api/seller/products",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (user.role != "SELLER" && user.role != "ADMIN") {
                replyJson(callback, errorJson("seller access required"),
                          k403Forbidden);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            Json::Value product;
            std::string err;
            if (!createProduct(user.id, body, product, err)) {
                HttpStatusCode code =
                    err == "name is required" ||
                            err.find("must be") != std::string::npos ||
                            err.find("image_url") != std::string::npos
                        ? k400BadRequest
                        : k500InternalServerError;
                replyError(callback, err, code);
                return;
            }
            product["seller_name"] = user.name;
            replyJson(callback, product, k201Created);
        },
        {Post});

    app().registerHandler(
        "/api/seller/products/{1}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (user.role != "SELLER" && user.role != "ADMIN") {
                replyJson(callback, errorJson("seller access required"),
                          k403Forbidden);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            Json::Value product;
            std::string err;
            bool isAdmin = user.role == "ADMIN";
            if (!updateProduct(id, user.id, isAdmin, body, product, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "product not found") code = k404NotFound;
                if (err == "permission denied") code = k403Forbidden;
                if (err == "name is required" ||
                    err.find("must be") != std::string::npos ||
                    err.find("image_url") != std::string::npos)
                    code = k400BadRequest;
                replyError(callback, err, code);
                return;
            }
            replyJson(callback, product);
        },
        {Put});

    app().registerHandler(
        "/api/seller/products/{1}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (user.role != "SELLER" && user.role != "ADMIN") {
                replyJson(callback, errorJson("seller access required"),
                          k403Forbidden);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            std::string err;
            bool isAdmin = user.role == "ADMIN";
            if (!setProductActive(id, user.id, isAdmin, false, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "product not found") code = k404NotFound;
                if (err == "permission denied") code = k403Forbidden;
                replyError(callback, err, code);
                return;
            }
            Json::Value out(Json::objectValue);
            out["status"] = "deactivated";
            replyJson(callback, out);
        },
        {Delete});
}

void registerAdminHandlers() {
    app().registerHandler(
        "/api/admin/users",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (user.role != "ADMIN") {
                replyJson(callback, errorJson("admin access required"),
                          k403Forbidden);
                return;
            }
            std::string role = req->getParameter("role");
            Json::Value users;
            std::string err;
            if (!adminUsersFiltered(role, users, err)) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, users);
        },
        {Get});

    app().registerHandler(
        "/api/admin/products",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (user.role != "ADMIN") {
                replyJson(callback, errorJson("admin access required"),
                          k403Forbidden);
                return;
            }
            Json::Value products;
            std::string err;
            if (!adminProducts(products, err)) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, products);
        },
        {Get});

    app().registerHandler(
        "/api/admin/products/{1}/active",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!commerceAdminOnly(req, callback, user)) return;
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            bool active = body.get("active", true).asBool();
            std::string err;
            if (!setProductActive(id, "", true, active, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "product not found") code = k404NotFound;
                replyError(callback, err, code);
                return;
            }
            Json::Value out(Json::objectValue);
            out["status"] = active ? "activated" : "deactivated";
            replyJson(callback, out);
        },
        {Put});

    app().registerHandler(
        "/api/admin/users/{1}/status",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!commerceAdminOnly(req, callback, user)) return;
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            if (id == user.id) {
                replyJson(callback, errorJson("cannot change your own status"),
                          k400BadRequest);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            bool active = body.get("active", true).asBool();
            std::string err;
            if (!setUserActive(id, active, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "user not found") code = k404NotFound;
                replyError(callback, err, code);
                return;
            }
            Json::Value out(Json::objectValue);
            out["status"] = active ? "activated" : "suspended";
            replyJson(callback, out);
        },
        {Put});

    app().registerHandler(
        "/api/admin/orders",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!commerceAdminOnly(req, callback, user)) return;
            Json::Value orders;
            std::string err;
            if (!adminOrders(orders, err)) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, orders);
        },
        {Get});

    app().registerHandler(
        "/api/admin/reviews",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!commerceAdminOnly(req, callback, user)) return;
            Json::Value reviews;
            std::string err;
            if (!adminReviews(reviews, err)) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, reviews);
        },
        {Get});
}

void registerReviewHandlers() {
    app().registerHandler(
        "/api/products/{1}/reviews",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            std::string viewerId;
            std::string viewerRole;
            if (authenticated(req, user)) {
                viewerId = user.id;
                viewerRole = user.role;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value out;
            std::string err;
            if (!productReviews(id, viewerId, viewerRole, out, err)) {
                replyError(callback, err,
                           err == "product not found" ? k404NotFound
                                                      : k500InternalServerError);
                return;
            }
            replyJson(callback, out);
        },
        {Get});

    app().registerHandler(
        "/api/products/{1}/reviews",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string rating = body.get("rating", "").asString();
            std::string title = body.get("title", "").asString();
            std::string comment = body.get("comment", "").asString();
            Json::Value review;
            std::string err;
            if (!addReview(id, user.id, user.role, rating, title, comment,
                           review, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "product not found") code = k404NotFound;
                if (err.find("already reviewed") != std::string::npos) {
                    code = k409Conflict;
                }
                if (err == "only buyers can review products" ||
                    err == "rating is required" ||
                    err.find("rating must") != std::string::npos ||
                    err == "you can only review a product you ordered and "
                           "received")
                    code = k400BadRequest;
                replyError(callback, err, code);
                return;
            }
            replyJson(callback, review, k201Created);
        },
        {Post});
}

void registerCommerceHandlers() {
    app().registerHandler(
        "/api/cart",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            replyCart(callback, user.id);
        },
        {Get});

    app().registerHandler(
        "/api/cart",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string productId = body.get("product_id", "").asString();
            std::string quantity = body.get("quantity", "1").asString();
            if (!validId(productId)) {
                replyJson(callback, errorJson("product not found or inactive"),
                          k400BadRequest);
                return;
            }
            std::string err;
            if (!addToCart(user.id, productId, quantity, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err.find("available in stock") != std::string::npos ||
                    err == "invalid quantity" ||
                    err == "product not found or inactive" ||
                    err == "product is out of stock")
                    code = k400BadRequest;
                replyError(callback, err, code);
                return;
            }
            replyCart(callback, user.id);
        },
        {Post});

    app().registerHandler(
        "/api/cart/{1}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string quantity = body.get("quantity", "1").asString();
            std::string err;
            if (!setCartQuantity(user.id, id, quantity, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "invalid quantity" ||
                    err == "product not found or inactive" ||
                    err.find("available in stock") != std::string::npos)
                    code = k400BadRequest;
                replyError(callback, err, code);
                return;
            }
            replyCart(callback, user.id);
        },
        {Put});

    app().registerHandler(
        "/api/cart/{1}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            std::string err;
            if (!removeCartItem(user.id, id, err)) {
                replyError(callback, err);
                return;
            }
            replyCart(callback, user.id);
        },
        {Delete});

    app().registerHandler(
        "/api/cart",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            std::string err;
            if (!clearCart(user.id, err)) {
                replyError(callback, err);
                return;
            }
            replyCart(callback, user.id);
        },
        {Delete});

    app().registerHandler(
        "/api/wishlist",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            std::string err;
            Json::Value items = getWishlist(user.id, err);
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            Json::Value out(Json::objectValue);
            out["wishlist"] = items;
            out["item_count"] = static_cast<int>(items.size());
            replyJson(callback, out);
        },
        {Get});

    app().registerHandler(
        "/api/wishlist",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string productId = body.get("product_id", "").asString();
            if (!validId(productId)) {
                replyJson(callback, errorJson("product not found or inactive"),
                          k400BadRequest);
                return;
            }
            std::string err;
            if (!addWishlist(user.id, productId, err)) {
                if (err == "product not found or inactive") {
                    replyJson(callback, errorJson(err), k400BadRequest);
                    return;
                }
                replyError(callback, err);
                return;
            }
            Json::Value items = getWishlist(user.id, err);
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            Json::Value out(Json::objectValue);
            out["wishlist"] = items;
            out["item_count"] = static_cast<int>(items.size());
            replyJson(callback, out);
        },
        {Post});

    app().registerHandler(
        "/api/wishlist/{1}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            std::string err;
            if (!removeWishlist(user.id, id, err)) {
                replyError(callback, err);
                return;
            }
            Json::Value items = getWishlist(user.id, err);
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            Json::Value out(Json::objectValue);
            out["wishlist"] = items;
            out["item_count"] = static_cast<int>(items.size());
            replyJson(callback, out);
        },
        {Delete});

    app().registerHandler(
        "/api/checkout",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string paymentMethod = body.get("payment_method", "").asString();
            std::string err;
            Json::Value order = placeOrder(user.id, paymentMethod, err);
            if (!err.empty()) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "cart is empty") code = k400BadRequest;
                if (err.find("insufficient stock") != std::string::npos) {
                    code = k409Conflict;
                }
                replyError(callback, err, code);
                return;
            }
            replyJson(callback, order, k201Created);
        },
        {Post});

    app().registerHandler(
        "/api/orders",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            std::string err;
            Json::Value orders = orderHistory(user.id, err);
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, orders);
        },
        {Get});

    app().registerHandler(
        "/api/orders/{1}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!authenticated(req, user)) {
                replyJson(callback, errorJson("not authenticated"),
                          k401Unauthorized);
                return;
            }
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value order;
            std::string err;
            bool isAdmin = user.role == "ADMIN";
            if (!orderDetail(id, user.id, isAdmin, order, err)) {
                replyError(callback, err,
                           err == "order not found" ? k404NotFound
                                                    : k500InternalServerError);
                return;
            }
            replyJson(callback, order);
        },
        {Get});

    app().registerHandler(
        "/api/seller/orders",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            User user;
            if (!commerceSellerOnly(req, callback, user)) return;
            std::string err;
            Json::Value orders = sellerOrders(user.id, err);
            if (!err.empty()) {
                replyError(callback, err);
                return;
            }
            replyJson(callback, orders);
        },
        {Get});

    app().registerHandler(
        "/api/orders/{1}/status",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& id) {
            User user;
            if (!commerceSellerOnly(req, callback, user)) return;
            if (!validId(id)) {
                replyJson(callback, errorJson("not found"), k404NotFound);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string status = body.get("status", "").asString();
            std::string err;
            bool isAdmin = user.role == "ADMIN";
            if (!updateOrderStatus(id, user.id, isAdmin, status, err)) {
                HttpStatusCode code = k500InternalServerError;
                if (err == "invalid status") code = k400BadRequest;
                if (err == "order not found") code = k404NotFound;
                if (err == "permission denied") code = k403Forbidden;
                replyError(callback, err, code);
                return;
            }
            Json::Value out(Json::objectValue);
            out["status"] = "updated";
            out["order_status"] = status;
            replyJson(callback, out);
        },
        {Put});
}

void registerAssistantHandlers() {
    app().registerHandler(
        "/api/assistant/ask",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            const std::string ip = req->getPeerAddr().toIp();
            if (assistantRateLimited(ip)) {
                replyJson(callback,
                          errorJson("too many requests, please try again "
                                    "later"),
                          k429TooManyRequests);
                return;
            }
            Json::Value body;
            auto json = req->getJsonObject();
            if (json) body = *json;
            std::string message = body.get("message", "").asString();

            size_t begin = message.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                message.clear();
            } else {
                message = message.substr(begin);
            }
            if (message.empty()) {
                replyJson(callback, errorJson("message is required"),
                          k400BadRequest);
                return;
            }
            if (message.size() > 500) {
                message = message.substr(0, 500);
            }

            Json::Value context = assistantContext();
            Json::Value local = assistantLocalAnswer(message, context);

            std::string baseUrl, key, model;
            if (!assistantProviderConfigured(baseUrl, key, model)) {
                replyJson(callback, local);
                return;
            }
            if (baseUrl.compare(0, 7, "http://") != 0 &&
                baseUrl.compare(0, 8, "https://") != 0) {
                local["source"] = "fallback";
                replyJson(callback, local);
                return;
            }

            std::string catalog;
            const Json::Value& products = context["products"];
            for (int i = 0; i < static_cast<int>(products.size()); ++i) {
                const Json::Value& p = products[i];
                catalog += "- " + p["name"].asString() + " (" +
                           p["category"].asString() + "), price " +
                           p["price"].asString() + ", stock " +
                           p["stock"].asString() + ", rating " +
                           p["rating"].asString() + "\n";
            }

            Json::Value payload(Json::objectValue);
            payload["model"] = model;
            Json::Value messages(Json::arrayValue);
            Json::Value sys(Json::objectValue);
            sys["role"] = "system";
            sys["content"] =
                "You are the JK Mart shopping assistant. Answer the customer "
                "using ONLY the live catalog below. Do not invent products or "
                "prices. Be concise and friendly.\n\nCatalog:\n" + catalog;
            Json::Value userMsg(Json::objectValue);
            userMsg["role"] = "user";
            userMsg["content"] = message;
            messages.append(sys);
            messages.append(userMsg);
            payload["messages"] = messages;
            payload["temperature"] = 0.3;

            auto client = HttpClient::newHttpClient(baseUrl, 8000);
            auto request = HttpRequest::newHttpJsonRequest(payload);
            request->setMethod(Post);
            request->addHeader("Authorization", "Bearer " + key);
            client->sendRequest(
                request,
                [callback, local](ReqResult result,
                                  const HttpResponsePtr& resp) {
                    auto fallback = [&callback, &local]() {
                        Json::Value fb = local;
                        fb["source"] = "fallback";
                        replyJson(callback, fb);
                    };
                    if (result != ReqResult::Ok || !resp) {
                        fallback();
                        return;
                    }
                    auto respJson = resp->getJsonObject();
                    std::string answer;
                    if (respJson) {
                        const Json::Value& j = *respJson;
                        if (j.isMember("choices") && j["choices"].isArray() &&
                            j["choices"].size() > 0 &&
                            j["choices"][0].isMember("message") &&
                            j["choices"][0]["message"].isMember("content")) {
                            answer = j["choices"][0]["message"]["content"]
                                         .asString();
                        } else if (j.isMember("answer")) {
                            answer = j["answer"].asString();
                        } else if (j.isMember("response")) {
                            answer = j["response"].asString();
                        } else if (j.isMember("text")) {
                            answer = j["text"].asString();
                        }
                    }
                    size_t b = answer.find_first_not_of(" \t\r\n");
                    if (b != std::string::npos) answer = answer.substr(b);
                    if (answer.empty()) {
                        fallback();
                        return;
                    }
                    Json::Value out(Json::objectValue);
                    out["answer"] = answer;
                    out["source"] = "provider";
                    replyJson(callback, out);
                });
        },
        {Post});
}

}  // namespace jkmart