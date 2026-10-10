#pragma once
#include <json/json.h>
#include <string>

namespace jkmart {

Json::Value productsList(const std::string& query,
                         const std::string& category,
                         const std::string& sort,
                         int limit,
                         std::string& err);

Json::Value productById(const std::string& id, std::string& err);

Json::Value sellerProducts(const std::string& sellerId, std::string& err);

Json::Value sellerStats(const std::string& sellerId, std::string& err);

bool adminUsers(Json::Value& out, std::string& err);

bool adminProducts(Json::Value& out, std::string& err);

bool categories(Json::Value& out, std::string& err);

bool createProduct(const std::string& sellerId,
                   const Json::Value& body,
                   Json::Value& product,
                   std::string& err);

bool updateProduct(const std::string& id,
                   const std::string& userId,
                   bool isAdmin,
                   const Json::Value& body,
                   Json::Value& product,
                   std::string& err);

bool setProductActive(const std::string& id,
                      const std::string& userId,
                      bool isAdmin,
                      bool active,
                      std::string& err);

}  // namespace jkmart