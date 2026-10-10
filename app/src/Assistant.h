#pragma once

#include <json/json.h>

#include <string>

namespace jkmart {

/* AI shopping assistant.
 *
 * Credentials are read from environment variables only:
 *   JK_AI_API_URL   - provider endpoint (optional)
 *   JK_AI_API_KEY   - provider key (optional)
 *   JK_AI_MODEL     - model name (optional, default "gpt-4o-mini")
 *
 * When no provider is configured (or the call fails/times out) the
 * assistant answers locally using real JK Mart product data.
 * Secrets are never returned to clients.
 */

/* True if the caller IP has exceeded the per-window request cap. */
bool assistantRateLimited(const std::string& ip);

/*
 * Builds a small product context object from real DB rows:
 *   { store, source, generated_at, products:[...], categories:[...] }
 * Returns a general context (no products) if the DB is unavailable.
 */
Json::Value assistantContext();

/*
 * Rule-based local answer built from the message + the product context.
 * Always returns a non-empty answer string in Json::Value {answer, source}.
 */
Json::Value assistantLocalAnswer(const std::string& message,
                                 const Json::Value& context);

/* Environment-provider configuration (empty string when unset). */
bool assistantProviderConfigured(std::string& baseUrl, std::string& key,
                                 std::string& model);

}  // namespace jkmart