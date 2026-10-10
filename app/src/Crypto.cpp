#include "Crypto.h"

#include <trantor/utils/Utilities.h>

#include <array>
#include <chrono>
#include <random>
#include <vector>

namespace jkmart {

namespace {

std::string toHex(const std::string& bytes) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(hexd[c >> 4]);
        out.push_back(hexd[c & 0xf]);
    }
    return out;
}

std::string hmacSha256(const std::string& key, const std::string& data) {
    constexpr size_t block = 64;
    std::array<unsigned char, block> keyBlock{};
    auto h = trantor::utils::sha256(key.data(), key.size());
    if (key.size() > block) {
        std::copy(std::begin(h.bytes), std::end(h.bytes), keyBlock.begin());
    } else {
        std::copy(key.begin(), key.end(), keyBlock.begin());
    }
    std::array<unsigned char, block> oPad;
    std::array<unsigned char, block> iPad;
    for (size_t i = 0; i < block; ++i) {
        oPad[i] = keyBlock[i] ^ 0x5c;
        iPad[i] = keyBlock[i] ^ 0x36;
    }
    std::string innerIpadsn;
    innerIpadsn.assign(iPad.begin(), iPad.end());
    innerIpadsn.append(data);
    auto inner = trantor::utils::sha256(innerIpadsn.data(), innerIpadsn.size());

    std::string outerOpadsn;
    outerOpadsn.assign(oPad.begin(), oPad.end());
    outerOpadsn.append(reinterpret_cast<const char*>(inner.bytes), sizeof(inner.bytes));
    auto outer = trantor::utils::sha256(outerOpadsn.data(), outerOpadsn.size());
    return std::string(reinterpret_cast<const char*>(outer.bytes),
                       sizeof(outer.bytes));
}

}  // namespace

std::string randomHex(size_t bytes) {
    static std::mt19937_64 gen(
        std::random_device{}() ^ static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string raw;
    raw.reserve(bytes);
    while (raw.size() < bytes) {
        uint64_t v = gen();
        for (int i = 0; i < 8 && raw.size() < bytes; ++i) {
            raw.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
        }
    }
    return toHex(raw);
}

std::string pbkdf2Sha256(const std::string& password,
                         const std::string& salt,
                         unsigned iterations,
                         size_t dkLen) {
    std::string out;
    out.reserve(dkLen);
    unsigned blockIndex = 1;
    while (out.size() < dkLen) {
        std::string block = salt;
        block.push_back(static_cast<char>((blockIndex >> 24) & 0xff));
        block.push_back(static_cast<char>((blockIndex >> 16) & 0xff));
        block.push_back(static_cast<char>((blockIndex >> 8) & 0xff));
        block.push_back(static_cast<char>(blockIndex & 0xff));
        ++blockIndex;

        std::string u = hmacSha256(password, block);
        std::string f = u;
        for (unsigned i = 1; i < iterations; ++i) {
            u = hmacSha256(password, u);
            for (size_t b = 0; b < u.size(); ++b) {
                f[b] = static_cast<char>(f[b] ^ u[b]);
            }
        }
        out.append(f.data(), f.size());
    }
    out.resize(dkLen);
    return out;
}

std::string hashPassword(const std::string& password) {
    const std::string salt = randomHex(16);
    constexpr unsigned iterations = 100000;
    const std::string dk = pbkdf2Sha256(password, salt, iterations, 32);
    return "pbkdf2_sha256$" + std::to_string(iterations) + "$" + salt + "$" +
           toHex(dk);
}

bool verifyPassword(const std::string& password,
                    const std::string& stored) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = stored.find('$', start);
        if (pos == std::string::npos) {
            parts.push_back(stored.substr(start));
            break;
        }
        parts.push_back(stored.substr(start, pos - start));
        start = pos + 1;
    }
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") {
        return false;
    }
    unsigned iterations = 0;
    try {
        iterations = static_cast<unsigned>(std::stoul(parts[1]));
    } catch (...) {
        return false;
    }
    const std::string dk = pbkdf2Sha256(password, parts[2], iterations, 32);
    return toHex(dk) == parts[3];
}

}  // namespace jkmart