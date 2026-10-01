// bad.cc -- adversarial: every function is a stub. no_stubs.py MUST flag this
// and exit nonzero. Each pattern below is a distinct stub class.

#include <string>
#include <vector>

namespace agency {

// 1) sole-body return nullptr;
const char* GetName() { return nullptr; }

// 2) sole-body return {};
std::vector<int> GetItems() { return {}; }

// 3) empty body
void Initialize() {}

// 4) TODO marker inside body
int ComputeScore() {
  // TODO: actually compute the score
  return 0;
}

// 5) NOTIMPLEMENTED() placeholder
void Render() {
  NOTIMPLEMENTED();
}

// 6) placeholder mojo status
int SendMessage() { return mojo::Status::Ok(); }

}  // namespace agency
