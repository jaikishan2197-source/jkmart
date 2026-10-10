#pragma once
#include <string>

namespace jkmart {

std::string randomHex(size_t bytes);
std::string pbkdf2Sha256(const std::string& password,
                         const std::string& salt,
                         unsigned iterations = 100000,
                         size_t dkLen = 32);

std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password,
                    const std::string& stored);

}  // namespace jkmart