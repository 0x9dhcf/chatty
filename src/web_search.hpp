#pragma once

#include <agt/json.hpp>
#include <agt/tool.hpp>
#include <cstdlib>
#include <curl/curl.h>
#include <string>

// Tavily-backed web search tool.
//
// Reads the API key from the TAVILY_API_KEY environment variable and POSTs
// to https://api.tavily.com/search via libcurl.
class WebSearch : public agt::Tool {
public:
  WebSearch() {
    // curl_global_init is reference-counted and thread-safe to call multiple
    // times. Pair with curl_global_cleanup in the destructor.
    curl_global_init(CURL_GLOBAL_DEFAULT);
  }
  ~WebSearch() override { curl_global_cleanup(); }

  WebSearch(const WebSearch&) = delete;
  WebSearch& operator=(const WebSearch&) = delete;
  WebSearch(WebSearch&&) = delete;
  WebSearch& operator=(WebSearch&&) = delete;

private:
  const char* name() const noexcept override { return "web_search"; }
  const char* description() const noexcept override {
    return "Search the web via Tavily and return a synthesized answer plus the top results "
           "(title, URL, snippet). Use for current events, recent docs, or any fact you "
           "cannot derive from local context.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"query", {{"type", "string"}, {"description", "the search query"}}},
              {"max_results",
               {{"type", "integer"},
                {"description", "number of results to return (1-10, default 5)"}}},
              {"search_depth",
               {{"type", "string"},
                {"enum", {"basic", "advanced"}},
                {"description", "basic is fast and cheap; advanced is deeper (default basic)"}}},
              {"topic",
               {{"type", "string"},
                {"enum", {"general", "news"}},
                {"description", "search topic (default general)"}}},
              {"include_answer",
               {{"type", "boolean"},
                {"description", "ask Tavily to synthesize a direct answer (default true)"}}}}},
            {"required", {"query"}}};
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

    agt::Json body = {
        {"query", input["query"].get<std::string>()},
        {"max_results", input.value("max_results", 5)},
        {"search_depth", input.value("search_depth", std::string{"basic"})},
        {"topic", input.value("topic", std::string{"general"})},
        {"include_answer", input.value("include_answer", true)},
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

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.tavily.com/search");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
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

    std::string out;
    if (resp.contains("answer") && resp["answer"].is_string()) {
      auto ans = resp["answer"].get<std::string>();
      if (!ans.empty())
        out += "Answer: " + ans + "\n\n";
    }

    if (resp.contains("results") && resp["results"].is_array()) {
      int i = 1;
      for (const auto& r : resp["results"]) {
        out += "[" + std::to_string(i++) + "] " + r.value("title", std::string{"(no title)"}) + "\n";
        out += "    " + r.value("url", std::string{}) + "\n";
        auto snippet = r.value("content", std::string{});
        constexpr size_t snippet_max = 600;
        if (snippet.size() > snippet_max)
          snippet.resize(snippet_max);
        if (!snippet.empty())
          out += "    " + snippet + "\n";
        out += "\n";
      }
    }

    if (out.empty())
      return "no results";
    return out;
  }
};
