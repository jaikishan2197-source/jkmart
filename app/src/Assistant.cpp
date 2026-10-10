#include "Assistant.h"

#include "Db.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace jkmart {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool hasAny(const std::string& text,
            std::initializer_list<const char*> needles) {
    for (const char* n : needles) {
        if (text.find(n) != std::string::npos) return true;
    }
    return false;
}

std::string money(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return "\xE2\x82\xB9" + std::string(buf);
}

/* ---------- rate limiting (per client address) ---------- */

std::mutex& rateMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>>&
rateWindows() {
    static std::unordered_map<std::string,
                              std::deque<std::chrono::steady_clock::time_point>>
        w;
    return w;
}

constexpr int kRateWindowSeconds = 60;
constexpr int kRateMaxRequests = 20;

}  // namespace

bool assistantRateLimited(const std::string& ip) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rateMutex());
    auto& q = rateWindows()[ip];
    while (!q.empty() &&
           now - q.front() > std::chrono::seconds(kRateWindowSeconds)) {
        q.pop_front();
    }
    if (q.size() >= static_cast<size_t>(kRateMaxRequests)) {
        return true;
    }
    q.push_back(now);
    return false;
}

Json::Value assistantContext() {
    Json::Value ctx(Json::objectValue);
    ctx["store"] = "JK Mart";
    ctx["currency"] = "USD";
    ctx["source"] = "live database";

    auto rows = Db::instance().query(
        "SELECT p.id, p.name, p.category, p.price, p.rating, p.stock"
        " FROM products p"
        " WHERE p.is_active = TRUE"
        " ORDER BY p.rating DESC, p.rating_count DESC, p.id DESC"
        " LIMIT 8");
    Json::Value products(Json::arrayValue);
    for (const auto& r : rows) {
        Json::Value p(Json::objectValue);
        p["id"] = r["id"].asString();
        p["name"] = r["name"].asString();
        p["category"] = r["category"].asString();
        p["price"] = r["price"].asString();
        p["rating"] = r["rating"].asString();
        p["stock"] = r["stock"].asString();
        products.append(p);
    }
    ctx["products"] = products;

    auto cats = Db::instance().query(
        "SELECT DISTINCT category FROM products WHERE is_active = TRUE"
        " ORDER BY category");
    Json::Value catList(Json::arrayValue);
    for (const auto& c : cats) {
        catList.append(c["category"].asString());
    }
    ctx["categories"] = catList;

    return ctx;
}

namespace {

Json::Value fallbackJson(const std::string& text, const char* source) {
    Json::Value out(Json::objectValue);
    out["answer"] = text;
    out["source"] = source;
    return out;
}

std::string listProducts(const Json::Value& products) {
    std::string s;
    for (int i = 0; i < static_cast<int>(products.size()); ++i) {
        const auto& p = products[i];
        if (!s.empty()) s += "\n";
        std::string star = p["rating"].asString();
        double priceValue = 0.0;
        try { priceValue = std::stod(p["price"].asString()); } catch (...) {}
        if (star != "0" && star != "0.0") {
            s += "- " + p["name"].asString() + " (" + p["category"].asString() +
                  ") - " + money(priceValue) + ", rating " + star + "/5";
        } else {
            s += "- " + p["name"].asString() + " (" + p["category"].asString() +
                  ") - " + money(priceValue);
        }
    }
    return s;
}

bool containsPrice(const std::string& text, double& price) {
    std::size_t pos;
    std::string digits;
    // patterns: "₹12.50", "12.50", or a bare integer after 'under'
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            digits.clear();
            std::size_t j = i;
            while (j < text.size() &&
                   (std::isdigit(static_cast<unsigned char>(text[j])) ||
                    text[j] == '.')) {
                digits.push_back(text[j]);
                ++j;
            }
            try {
                price = std::stod(digits);
                if (price > 0) {
                    pos = i;
                    (void)pos;
                    return true;
                }
            } catch (...) {
                break;
            }
            i = j;
        }
    }
    return false;
}

}  // namespace

Json::Value assistantLocalAnswer(const std::string& message,
                                 const Json::Value& context) {
    const std::string m = lower(message);
    const Json::Value& products = context["products"];
    Json::Value reply;

    /* Budget question: "products under ₹500" */
    if (hasAny(m, {"under ", "less than", "below "})) {
        double maxPrice = -1.0;
        if (containsPrice(m, maxPrice)) {
            Json::Value matches(Json::arrayValue);
            for (const auto& p : products) {
                double price = 0.0;
                try {
                    price = std::stod(p["price"].asString());
                } catch (...) {
                    continue;
                }
                if (price <= maxPrice) matches.append(p);
            }
            if (!matches.empty()) {
                std::string answer =
                    "Here are products at or under " + money(maxPrice) +
                    ":\n" + listProducts(matches);
                reply["answer"] = answer;
            } else {
                reply["answer"] =
                    "I could not find any in-stock products under " +
                    money(maxPrice) +
                    " right now. The cheapest current items start around ";
        double cheapValue = 0.0;
        try { cheapValue = std::stod(products[0]["price"].asString()); } catch (...) {}
        reply["answer"] =
            reply["answer"].asString() + money(cheapValue) + ".";
            }
        } else {
            reply["answer"] =
                "I did not catch a price limit. Tell me a budget such as "
                "\"products under " "\xE2\x82\xB9" "500\" and I will list what fits.";
        }
        reply["source"] = "local";
        return reply;
    }

    /* Category question */
    const Json::Value& cats = context["categories"];
    for (const auto& c : cats) {
        const std::string cat = lower(c.asString());
        if (m.find(cat) != std::string::npos) {
            Json::Value matches(Json::arrayValue);
            for (const auto& p : products) {
                if (lower(p["category"].asString()) == cat) matches.append(p);
            }
            if (!matches.empty()) {
                reply["answer"] =
                    "The " + c.asString() + " category has: \n" +
                    listProducts(matches);
            } else {
                reply["answer"] =
                    "We carry the " + c.asString() +
                    " category, but these products are currently out of "
                    "stock or unavailable.";
            }
            reply["source"] = "local";
            return reply;
        }
    }

    /* Recommendations */
    if (hasAny(m, {"recommend", "suggest", "best", "top "})) {
        Json::Value rated(Json::arrayValue);
        for (const auto& p : products) {
            double r = 0.0;
            try {
                r = std::stod(p["rating"].asString());
            } catch (...) {
                continue;
            }
            if (r > 0) rated.append(p);
        }
        if (!rated.empty()) {
            reply["answer"] =
                "Based on shopper ratings, I recommend:\n" +
                listProducts(rated);
        } else {
            reply["answer"] =
                "We have " + std::to_string(products.size()) +
                " products in the catalog. Here are the current ones:\n" +
                listProducts(products);
        }
        reply["source"] = "local";
        return reply;
    }

    /* Stock availability */
    if (hasAny(m, {"stock", "available", "in stock"})) {
        int available = 0;
        for (const auto& p : products) {
            try {
                if (std::stoi(p["stock"].asString()) > 0) ++available;
            } catch (...) {
            }
        }
        std::string answer =
            std::to_string(available) + " of " +
            std::to_string(products.size()) +
            " top products are currently in stock, for example:\n" +
            listProducts(products);
        for (const auto& p : products) {
            if (m.find(lower(p["name"].asString())) != std::string::npos) {
                answer += "\n\"" + p["name"].asString() + "\" has " +
                          p["stock"].asString() + " units in stock.";
                break;
            }
        }
        reply["answer"] = answer;
        reply["source"] = "local";
        return reply;
    }

    /* Product-name lookup */
    for (const auto& p : products) {
        std::string pname = lower(p["name"].asString());
        if (m.find(pname) != std::string::npos) {
            std::string answer =
                p["name"].asString() + " (" + p["category"].asString() +
                ") is " + p["price"].asString();
            try {
                int stock = std::stoi(p["stock"].asString());
                answer += " and has " + std::to_string(stock) +
                          (stock == 1 ? " unit" : " units") + " in stock.";
            } catch (...) {
            }
            reply["answer"] = answer;
            reply["source"] = "local";
            return reply;
        }
    }

    /* FAQ answers */
    if (hasAny(m, {"deliver", "shipping", "delivery", "how long"})) {
        reply["answer"] =
            "JK Mart orders are typically shipped within 1 business day and "
            "delivered in 3-5 business days. Order status can be tracked on "
            "the Orders page.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"return", "refund"})) {
        reply["answer"] =
            "Items can be returned within 7 days of delivery if they are "
            "unused and in original packaging. Refunds are processed to the "
            "original payment method.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"payment", "pay", "card"})) {
        reply["answer"] =
            "This demo store uses a simulated DEMO payment only. No real "
            "card or payment details are collected or stored.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"how to order", "how do i buy", "checkout", "place an order"})) {
        reply["answer"] =
            "To order: browse products, add items to your cart, then go to "
            "Checkout and confirm with the DEMO payment. You will see the "
            "order in your Orders history immediately.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"register", "create account", "sign up"})) {
        reply["answer"] =
            "You can register as a Buyer or Seller from the Register page. "
            "Just choose your role, pick a password of at least 6 characters, "
            "and log in.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"review", "rating"})) {
        reply["answer"] =
            "You can rate a product (1-5 stars) after an order containing it "
            "has been delivered. One review per product per account. Ratings "
            "update the product average shown in the catalog.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"contact", "support", "help"})) {
        reply["answer"] =
            "For help, visit the Support section of the website or send a "
            "message through the contact form. We usually reply within one "
            "business day.";
        reply["source"] = "local";
        return reply;
    }
    if (hasAny(m, {"cancel", "cancel my order"})) {
        reply["answer"] =
            "Pending orders can be cancelled from the Orders page before they "
            "are delivered.";
        reply["source"] = "local";
        return reply;
    }

    /* Generic welcome + suggestions */
    std::string categoriesSummary;
    for (int i = 0; i < static_cast<int>(cats.size()); ++i) {
        if (i > 0) categoriesSummary += ", ";
        categoriesSummary += cats[i].asString();
    }
    std::string answer =
        "Hi! I am the JK Mart shopping assistant. I can help you by looking "
        "at our live catalog.";
    if (!products.empty()) {
        answer += " Right now we have products in these categories: " +
                  categoriesSummary + ".";
    }
    answer +=
        "\n\nAsk me things like:\n"
        "- \"Show me products under " "\xE2\x82\xB9" "500\"\n"
        "- \"Best ratings in Snacks\"\n"
        "- \"Recommend something\"\n"
        "- \"When will my order arrive?\"";
    reply["answer"] = answer;
    reply["source"] = "local";
    return reply;
}

bool assistantProviderConfigured(std::string& baseUrl, std::string& key,
                                 std::string& model) {
    const char* url = std::getenv("JK_AI_API_URL");
    const char* k = std::getenv("JK_AI_API_KEY");
    const char* mdl = std::getenv("JK_AI_MODEL");
    baseUrl = url ? url : "";
    key = k ? k : "";
    model = mdl && *mdl ? mdl : "gpt-4o-mini";
    return !baseUrl.empty() && !key.empty();
}

}  // namespace jkmart