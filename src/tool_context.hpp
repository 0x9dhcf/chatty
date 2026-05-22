#pragma once

#include <functional>

namespace ptty {
class LineEditor;
}

class ProcessRegistry;

// Bundle of references the host (Chatty) makes available to tool::execute()
// via its `void* context` parameter. Tools cast back to this struct and
// use whichever members they need.
struct ChattyToolContext {
  ptty::LineEditor* editor = nullptr;
  ProcessRegistry* registry = nullptr;
  // Tools that mutate state visible in the prompt (e.g. supervised-process
  // count) call this so the editor re-renders the prompt on its next refresh.
  // Cheap to invoke even if nothing changed.
  std::function<void()> refresh_prompt;
};
