#pragma once
#include <string>

namespace jkmart {

struct User {
    std::string id;
    std::string name;
    std::string email;
    std::string role;
    std::string passwordHash;
    bool isActive = true;
};

bool authRegister(const std::string& name,
                  const std::string& email,
                  const std::string& password,
                  const std::string& role,
                  User& outUser,
                  std::string& err);

bool authLogin(const std::string& email,
               const std::string& password,
               User& outUser,
               std::string& err);

std::string createSession(const User& user);
bool userByToken(const std::string& token, User& user);
void destroySession(const std::string& token);

bool hasRole(const User& user, const std::string& role);

}  // namespace jkmart