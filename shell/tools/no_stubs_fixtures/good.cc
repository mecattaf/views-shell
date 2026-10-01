// good.cc -- clean, non-trivial C++ bodies. no_stubs.py MUST exit 0 on this.
//
// Every function here does real work, and the early-return guards below are
// exactly the case a regex would false-positive on but the AST handles.

#include <map>
#include <string>
#include <vector>

namespace agency {

// Non-trivial arithmetic.
int Add(int a, int b) {
  int sum = a + b;
  return sum;
}

// Legitimate early-return guard: `return nullptr;` is NOT the sole body,
// so this must NOT be flagged.
const char* LookupOrNull(const std::map<std::string, std::string>& m,
                         const std::string& key) {
  auto it = m.find(key);
  if (it == m.end()) {
    return nullptr;  // guard, not a stub
  }
  return it->second.c_str();
}

// `return {};` here is a real early-out on bad input, not a sole-body stub.
std::vector<int> EvensUpTo(int n) {
  if (n < 0) {
    return {};  // guard
  }
  std::vector<int> out;
  for (int i = 0; i <= n; i += 2) {
    out.push_back(i);
  }
  return out;
}

// Real branching logic.
int Classify(int x) {
  if (x < 0) {
    return -1;
  } else if (x == 0) {
    return 0;
  }
  return 1;
}

class Widget {
 public:
  // Empty special members are legitimate and exempt.
  Widget() = default;
  ~Widget() {}

  // Real method body.
  int DoubleValue() const { return value_ * 2; }

  void SetValue(int v) {
    value_ = v;
    dirty_ = true;
  }

 private:
  int value_ = 0;
  bool dirty_ = false;
};

}  // namespace agency
