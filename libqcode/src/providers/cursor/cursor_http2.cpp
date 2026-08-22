#include "cursor_http2.h"

#include <qcode/core/logger.h>

#include <curl/curl.h>

namespace qcode {
namespace cursor {
namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

struct StreamWriteCtx {
  std::string* body = nullptr;
  const std::function<bool(std::string_view)>* on_chunk = nullptr;
  bool abort = false;
};

size_t stream_write_callback(char* ptr, size_t size, size_t nmemb,
                             void* userdata) {
  auto* ctx = static_cast<StreamWriteCtx*>(userdata);
  const size_t n = size * nmemb;
  if (ctx->body != nullptr) {
    ctx->body->append(ptr, n);
  }
  if (ctx->on_chunk != nullptr && *(ctx->on_chunk) != nullptr) {
    if (!(*ctx->on_chunk)(std::string_view(ptr, n))) {
      ctx->abort = true;
      return 0;  // abort transfer
    }
  }
  return n;
}

struct CurlGlobal {
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

void ensure_curl() { static CurlGlobal global; }

void perform_post(
    Http2PostResult& result,
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body,
    const std::string& content_type,
    curl_write_callback write_fn,
    void* write_data,
    int timeout_sec) {
  ensure_curl();

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    result.error = "curl_easy_init failed";
    return;
  }

  struct curl_slist* header_list = nullptr;
  header_list =
      curl_slist_append(header_list, ("Content-Type: " + content_type).c_str());
  for (const auto& [name, value] : headers) {
    header_list =
        curl_slist_append(header_list, (name + ": " + value).c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_fn);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_data);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_sec));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

  const CURLcode code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);

  if (code == CURLE_OK) {
    LOG_DEBUG("Cursor HTTP/2 POST status={} body_size={} url={}", result.status,
              result.body.size(), url);
  } else if (code == CURLE_WRITE_ERROR) {
    result.aborted = true;
    LOG_DEBUG("Cursor HTTP/2 POST aborted by callback status={} body_size={}",
              result.status, result.body.size());
  } else {
    result.error = std::string("curl: ") + curl_easy_strerror(code);
    LOG_ERROR("Cursor HTTP/2 POST failed: {} url={}", result.error, url);
  }

  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
}

}  // namespace

Http2PostResult http2_post(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body,
    const std::string& content_type,
    int timeout_sec) {
  Http2PostResult result;
  perform_post(result, url, headers, body, content_type, write_callback,
               &result.body, timeout_sec);
  return result;
}

Http2PostResult http2_post_stream(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body,
    const std::string& content_type,
    const std::function<bool(std::string_view chunk)>& on_chunk,
    int timeout_sec) {
  Http2PostResult result;
  StreamWriteCtx ctx;
  ctx.body = &result.body;
  ctx.on_chunk = &on_chunk;
  perform_post(result, url, headers, body, content_type, stream_write_callback,
               &ctx, timeout_sec);
  if (ctx.abort) {
    result.aborted = true;
  }
  return result;
}

}  // namespace cursor
}  // namespace qcode
