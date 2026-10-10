#pragma once
#include <json/json.h>
#include <string>

namespace jkmart {

bool adminUsersFiltered(const std::string& role,
                        Json::Value& out,
                        std::string& err);

bool setUserActive(const std::string& userId,
                   bool active,
                   std::string& err);

bool adminOrders(Json::Value& out, std::string& err);

bool adminReviews(Json::Value& out, std::string& err);

}  // namespace jkmart