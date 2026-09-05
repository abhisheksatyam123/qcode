#include "cursor_http2.h"

#include <qcode/core/logger.h>

#include <curl/curl.h>
#include <functional>
#include <memory>

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
  std::function<bool()> should_abort;
  bool abort = false;
};

bool abort_requested(const StreamWriteCtx* ctx) {
  return ctx != nullptr && ctx->should_abort && ctx->should_abort();
}

size_t stream_write_callback(char* ptr, size_t size, size_t nmemb,
                             void* userdata) {
  auto* ctx = static_cast<StreamWriteCtx*>(userdata);
  if (abort_requested(ctx)) {
    ctx->abort = true;
    return 0;
  }
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

int stream_xferinfo(void* userdata, curl_off_t, curl_off_t, curl_off_t,
                    curl_off_t) {
  auto* ctx = static_cast<StreamWriteCtx*>(userdata);
  if (abort_requested(ctx)) {
    ctx->abort = true;
    return 1;
  }
  return 0;
}

struct CurlGlobal {
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

void ensure_curl() { static CurlGlobal global; }

void configure_post(
    CURL* curl,
    const std::string& url,
    struct curl_slist* header_list,
    const std::string& body,
    curl_write_callback write_fn,
    void* write_data,
    int timeout_sec) {
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_fn);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_data);
  // 0 = no total-time cap. RunSSE stays open for the whole agent turn;
  // a 300s CURLOPT_TIMEOUT aborted live builds at exactly 5 minutes.
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                   timeout_sec > 0 ? static_cast<long>(timeout_sec) : 0L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
}

struct curl_slist* make_header_list(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& content_type) {
  struct curl_slist* header_list = nullptr;
  header_list =
      curl_slist_append(header_list, ("Content-Type: " + content_type).c_str());
  for (const auto& [name, value] : headers) {
    header_list =
        curl_slist_append(header_list, (name + ": " + value).c_str());
  }
  return header_list;
}

void finish_post(CURL* curl, Http2PostResult& result, const std::string& url) {
  const CURLcode code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);

  if (code == CURLE_OK) {
    LOG_DEBUG("Cursor HTTP/2 POST status={} body_size={} url={}", result.status,
              result.body.size(), url);
  } else if (code == CURLE_WRITE_ERROR || code == CURLE_ABORTED_BY_CALLBACK) {
    result.aborted = true;
    LOG_DEBUG("Cursor HTTP/2 POST aborted by callback status={} body_size={}",
              result.status, result.body.size());
  } else {
    result.error = std::string("curl: ") + curl_easy_strerror(code);
    LOG_ERROR("Cursor HTTP/2 POST failed: {} url={}", result.error, url);
  }
}

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

  auto* header_list = make_header_list(headers, content_type);
  configure_post(curl, url, header_list, body, write_fn, write_data,
                 timeout_sec);
  finish_post(curl, result, url);
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
    int timeout_sec,
    std::function<bool()> should_abort) {
  Http2PostResult result;
  StreamWriteCtx ctx;
  ctx.body = &result.body;
  ctx.on_chunk = &on_chunk;
  ctx.should_abort = std::move(should_abort);
  ensure_curl();

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    result.error = "curl_easy_init failed";
    return result;
  }

  auto* header_list = make_header_list(headers, content_type);
  configure_post(curl, url, header_list, body, stream_write_callback, &ctx,
                 timeout_sec);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, stream_xferinfo);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
  finish_post(curl, result, url);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  if (ctx.abort) {
    result.aborted = true;
  }
  return result;
}

struct Http2Client::Impl {
  CURL* curl = nullptr;

  Impl() {
    ensure_curl();
    curl = curl_easy_init();
  }

  ~Impl() {
    if (curl != nullptr) {
      curl_easy_cleanup(curl);
    }
  }
};

Http2Client::Http2Client() : impl_(std::make_unique<Impl>()) {}

Http2Client::~Http2Client() = default;

Http2PostResult Http2Client::post(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body,
    const std::string& content_type,
    int timeout_sec) {
  Http2PostResult result;
  if (impl_->curl == nullptr) {
    result.error = "curl_easy_init failed";
    return result;
  }

  curl_easy_reset(impl_->curl);
  auto* header_list = make_header_list(headers, content_type);
  configure_post(impl_->curl, url, header_list, body, write_callback,
                 &result.body, timeout_sec);
  finish_post(impl_->curl, result, url);
  curl_slist_free_all(header_list);
  return result;
}

}  // namespace cursor
}  // namespace qcode
