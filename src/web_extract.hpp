#pragma once

#include <agt/json.hpp>
#include <agt/tool.hpp>
#include <cstdlib>
#include <curl/curl.h>
#include <string>
#include <vector>

// Tavily-backed page extractor.
//
// Hits POST https://api.tavily.com/extract to fetch one or more URLs and
// return their cleaned content (markdown by default), boilerplate stripped
// server-side. Reads the API key from TAVILY_API_KEY.
class WebExtract : public agt::Tool {
public:
  WebExtract() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~WebExtract() override { curl_global_cleanup(); }

  WebExtract(const WebExtract&) = delete;
  WebExtract& operator=(const WebExtract&) = delete;
  WebExtract(WebExtract&&) = delete;
  WebExtract& operator=(WebExtract&&) = delete;

private:
  const char* name() const noexcept override { return "web_extract"; }
  const char* description() const noexcept override {
    return "Fetch one or more URLs via Tavily and return their cleaned, structured content "
           "(markdown by default). Use to read a specific page in full after web_search has "
           "surfaced it. Prefer 'basic' extract_depth; reach for 'advanced' only on pages "
           "where basic returns too little.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"urls",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "list of absolute http(s) URLs to extract"}}},
              {"extract_depth",
               {{"type", "string"},
                {"enum", {"basic", "advanced"}},
                {"description", "basic is fast/cheap; advanced costs more (default basic)"}}},
              {"format",
               {{"type", "string"},
                {"enum", {"markdown", "text"}},
                {"description", "output format (default markdown)"}}}}},
            {"required", {"urls"}}};
  }

  static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    (void)context;
    const char* key = std::getenv("TAVILY_API_KEY");
    if (key == nullptr || key[0] == '\0')
      return "error: TAVILY_API_KEY not set in environment";

    if (!input.contains("urls") || !input["urls"].is_array() || input["urls"].empty())
      return "error: 'urls' must be a non-empty array of strings";

    std::vector<std::string> urls;
    for (const auto& u : input["urls"]) {
      if (!u.is_string())
        return "error: each url must be a string";
      urls.push_back(u.get<std::string>());
    }

    agt::Json body = {
        {"urls", urls},
        {"extract_depth", input.value("extract_depth", std::string{"basic"})},
        {"format", input.value("format", std::string{"markdown"})},
        {"include_images", false},
    };
    std::string payload = body.dump();

    CURL* curl = curl_easy_init();
    if (!curl)
      return "error: curl_easy_init failed";

    std::string response;
    long http_code = 0;
    char errbuf[CURL_ERROR_SIZE] = {};

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    std::string auth_header = "Authorization: Bearer ";
    auth_header += key;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.tavily.com/extract");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "chatty/0.1 (+libcurl)");

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
      const char* detail = (errbuf[0] != '\0') ? errbuf : curl_easy_strerror(rc);
      return std::string("error: curl: ") + detail;
    }

    if (http_code < 200 || http_code >= 300) {
      std::string msg = "error: HTTP " + std::to_string(http_code);
      if (!response.empty()) {
        if (response.size() > 500)
          response.resize(500);
        msg += ": " + response;
      }
      return msg;
    }

    if (response.empty())
      return "error: empty response from Tavily";

    agt::Json resp;
    try {
      resp = agt::Json::parse(response);
    } catch (const std::exception&) {
      if (response.size() > 500)
        response.resize(500);
      return "error: non-JSON response: " + response;
    }

    constexpr std::size_t per_page_max = 12000;
    std::string out;

    if (resp.contains("results") && resp["results"].is_array()) {
      for (const auto& r : resp["results"]) {
        out += "--- " + r.value("url", std::string{"(unknown url)"}) + " ---\n";
        auto content = r.value("raw_content", std::string{});
        if (content.size() > per_page_max) {
          auto total = content.size();
          content.resize(per_page_max);
          content += "\n... (truncated, " + std::to_string(per_page_max) + " of " +
                     std::to_string(total) + " bytes)";
        }
        out += content + "\n\n";
      }
    }

    if (resp.contains("failed_results") && resp["failed_results"].is_array() &&
        !resp["failed_results"].empty()) {
      out += "--- failed ---\n";
      for (const auto& f : resp["failed_results"]) {
        out += "- " + f.value("url", std::string{"?"}) + ": " +
               f.value("error", std::string{"unknown error"}) + "\n";
      }
    }

    if (out.empty())
      return "no content extracted";
    return out;
  }
};
