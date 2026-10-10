#pragma once
#include <json/json.h>
#include <string>

namespace jkmart {

bool productReviews(const std::string& productId,
                    const std::string& viewerId,
                    const std::string& viewerRole,
                    Json::Value& out,
                    std::string& err);

bool addReview(const std::string& productId,
               const std::string& userId,
               const std::string& role,
               const std::string& rating,
               const std::string& title,
               const std::string& comment,
               Json::Value& out,
               std::string& err);

}  // namespace jkmart