#pragma once

#include <agt/tool.hpp>
#include <promptty/promptty.hpp>
#include <string>
#include <vector>

class Ask : public agt::Tool {
  const char* name() const noexcept override { return "ask"; }
  const char* description() const noexcept override {
    return "Present an interactive choice menu to the user and return their selection. "
           "Use when a request is ambiguous and there are several reasonable ways to "
           "fulfill it. Do not use for trivial or straightforward requests.";
  }

  agt::Json parameters() const override {
    return {{"type", "object"},
            {"properties",
             {{"choices",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "list of choices to present to the user"}}}}},
            {"required", {"choices"}}};
  }

  agt::Json execute(const agt::Json& input, void* context = nullptr) override {
    auto* editor = static_cast<ptty::LineEditor*>(context);
    if (!editor)
      return {{"error", "no editor context"}};

    std::vector<std::string> choices;
    for (const auto& c : input["choices"])
      choices.push_back(c.get<std::string>());

    if (choices.empty())
      return {{"error", "empty choices"}};

    auto result = editor->choose(choices);
    if (!result)
      return {{"error", "user cancelled"}};

    return result->value;
  }
};
