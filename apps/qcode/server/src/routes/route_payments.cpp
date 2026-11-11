// Razorpay Standard Checkout: order creation and payment signature verification.
//
// Credentials come from the environment (RAZORPAY_KEY_ID, RAZORPAY_KEY_SECRET).
// KEY_SECRET is never sent to the browser; only KEY_ID is exposed, via
// GET /api/payments/config.

#include "routes_internal.h"

#include <qcode/core/logger.h>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

namespace qcode {
namespace server {

namespace {

constexpr const char* kRazorpayBaseUrl = "https://api.razorpay.com";
constexpr long long kMinAmountPaise = 100;
constexpr size_t kMaxReceiptLength = 40;

// Unset and empty both count as "not configured".
std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

// Lowercase hex of HMAC-SHA256(key, message), the form Razorpay signs with.
std::string hmac_sha256_hex(const std::string& key, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    const unsigned char* msg = reinterpret_cast<const unsigned char*>(message.data());
    if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), msg, message.size(),
              digest, &digest_len)) {
        return {};
    }
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0f]);
    }
    return out;
}

// Razorpay error bodies look like {"error":{"code":"...","description":"..."}}.
std::string razorpay_error_description(const nlohmann::json& body) {
    if (body.is_object() && body.contains("error") && body["error"].is_object()) {
        return body["error"].value("description", std::string());
    }
    return {};
}

void handle_payment_config(const httplib::Request&, httplib::Response& res) {
    const std::string key_id = env_value("RAZORPAY_KEY_ID");
    if (key_id.empty()) {
        send_error(res, 500, "payments are not configured: set RAZORPAY_KEY_ID");
        return;
    }
    res.set_content(nlohmann::json({{"key_id", key_id}}).dump(), "application/json");
}

void handle_create_order(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    if (!parse_object_body(req.body, res, body)) return;
    if (!check_fields(body, res, {{"amount", FieldType::Number},
                                  {"currency", FieldType::String},
                                  {"receipt", FieldType::String}})) {
        return;
    }

    // Amount is in paise (1 INR = 100 paise) and must be an integer.
    const auto amount_it = body.find("amount");
    if (amount_it == body.end()) {
        send_error(res, 400, "amount is required (integer, in paise)");
        return;
    }
    if (!amount_it->is_number_integer() || amount_it->get<long long>() < kMinAmountPaise) {
        send_error(res, 400, "amount must be an integer of at least 100 paise");
        return;
    }
    const long long amount = amount_it->get<long long>();

    std::string currency = body.value("currency", std::string("INR"));
    if (currency.size() != 3 ||
        !std::all_of(currency.begin(), currency.end(), [](char c) { return c >= 'A' && c <= 'Z'; })) {
        send_error(res, 400, "currency must be a 3-letter ISO code, e.g. INR");
        return;
    }

    std::string receipt = body.value("receipt", std::string());
    if (receipt.empty()) {
        receipt = "rcpt_" + std::to_string(std::time(nullptr));
    } else if (receipt.size() > kMaxReceiptLength) {
        send_error(res, 400, "receipt must be at most 40 characters");
        return;
    }

    const std::string key_id = env_value("RAZORPAY_KEY_ID");
    const std::string key_secret = env_value("RAZORPAY_KEY_SECRET");
    if (key_id.empty() || key_secret.empty()) {
        send_error(res, 500, "payments are not configured: set RAZORPAY_KEY_ID and RAZORPAY_KEY_SECRET");
        return;
    }

    httplib::Client cli(kRazorpayBaseUrl);
    cli.set_basic_auth(key_id, key_secret);
    cli.set_connection_timeout(std::chrono::seconds(10));
    cli.set_read_timeout(std::chrono::seconds(20));
    cli.set_write_timeout(std::chrono::seconds(20));
    cli.enable_server_certificate_verification(true);

    const nlohmann::json order_request = {
        {"amount", amount},
        {"currency", currency},
        {"receipt", receipt},
        {"payment_capture", 1},
    };
    auto result = cli.Post("/v1/orders", order_request.dump(), "application/json");
    if (!result) {
        LOG_ERROR("Razorpay order request failed: {}", httplib::to_string(result.error()));
        send_error(res, 500, "could not reach Razorpay to create the order");
        return;
    }

    const nlohmann::json order = nlohmann::json::parse(result->body, nullptr, /*allow_exceptions=*/false);
    if (result->status == 401) {
        LOG_ERROR("Razorpay rejected the API credentials (HTTP 401)");
        send_error(res, 401, "Razorpay authentication failed: check RAZORPAY_KEY_ID and RAZORPAY_KEY_SECRET");
        return;
    }
    if (result->status < 200 || result->status >= 300 || order.is_discarded() ||
        !order.is_object() || !order.contains("id") || !order["id"].is_string()) {
        const std::string description = razorpay_error_description(order);
        LOG_ERROR("Razorpay order creation failed (HTTP {}): {}", result->status, description);
        send_error(res, 500, description.empty() ? "Razorpay order creation failed"
                                                 : "Razorpay order creation failed: " + description);
        return;
    }

    res.set_content(nlohmann::json({
                        {"order_id", order["id"].get<std::string>()},
                        {"amount", order.value("amount", amount)},
                        {"currency", order.value("currency", currency)},
                    }).dump(),
                    "application/json");
}

void handle_verify_payment(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    if (!parse_object_body(req.body, res, body)) return;
    if (!check_fields(body, res, {{"razorpay_order_id", FieldType::String},
                                  {"razorpay_payment_id", FieldType::String},
                                  {"razorpay_signature", FieldType::String}})) {
        return;
    }
    for (const char* name : {"razorpay_order_id", "razorpay_payment_id", "razorpay_signature"}) {
        if (body.value(name, std::string()).empty()) {
            send_error(res, 400, std::string("missing field '") + name + "'");
            return;
        }
    }

    const std::string key_secret = env_value("RAZORPAY_KEY_SECRET");
    if (key_secret.empty()) {
        send_error(res, 500, "payments are not configured: set RAZORPAY_KEY_SECRET");
        return;
    }

    const std::string order_id = body["razorpay_order_id"].get<std::string>();
    const std::string payment_id = body["razorpay_payment_id"].get<std::string>();
    const std::string signature = body["razorpay_signature"].get<std::string>();

    // Razorpay: signature = HMAC_SHA256(order_id + "|" + payment_id, KEY_SECRET).
    if (!razorpay_signature_matches(key_secret, order_id, payment_id, signature)) {
        LOG_WARN("Payment signature mismatch for order {}", order_id);
        send_error(res, 400, "payment signature verification failed");
        return;
    }

    res.set_content(nlohmann::json({
                        {"status", "verified"},
                        {"order_id", order_id},
                        {"payment_id", payment_id},
                    }).dump(),
                    "application/json");
}

}  // namespace

bool razorpay_signature_matches(const std::string& key_secret, const std::string& order_id,
                                const std::string& payment_id, const std::string& signature) {
    const std::string expected = hmac_sha256_hex(key_secret, order_id + "|" + payment_id);
    return !expected.empty() && constant_time_equal(expected, signature);
}

void register_payment_routes(httplib::Server& svr) {
    svr.Get("/api/payments/config", handle_payment_config);
    svr.Post("/api/create-order", handle_create_order);
    svr.Post("/api/verify-payment", handle_verify_payment);
}

}  // namespace server
}  // namespace qcode
