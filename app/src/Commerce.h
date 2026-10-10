#pragma once
#include <json/json.h>
#include <string>

namespace jkmart {

/* Cart */
Json::Value getCart(const std::string& buyerId, std::string& err);

bool addToCart(const std::string& buyerId,
               const std::string& productId,
               const std::string& quantity,
               std::string& err);

bool setCartQuantity(const std::string& buyerId,
                     const std::string& productId,
                     const std::string& quantity,
                     std::string& err);

bool removeCartItem(const std::string& buyerId,
                    const std::string& productId,
                    std::string& err);

bool clearCart(const std::string& buyerId, std::string& err);

/* Wishlist */
Json::Value getWishlist(const std::string& buyerId, std::string& err);

bool addWishlist(const std::string& buyerId,
                 const std::string& productId,
                 std::string& err);

bool removeWishlist(const std::string& buyerId,
                    const std::string& productId,
                    std::string& err);

/* Orders */
Json::Value placeOrder(const std::string& buyerId,
                       const std::string& paymentMethod,
                       std::string& err);

Json::Value orderHistory(const std::string& buyerId, std::string& err);

bool orderDetail(const std::string& orderId,
                 const std::string& userId,
                 bool isAdmin,
                 Json::Value& out,
                 std::string& err);

Json::Value sellerOrders(const std::string& sellerId, std::string& err);

bool updateOrderStatus(const std::string& orderId,
                       const std::string& userId,
                       bool isAdmin,
                       const std::string& status,
                       std::string& err);

}  // namespace jkmart