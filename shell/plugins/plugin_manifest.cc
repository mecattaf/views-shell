// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/plugin_manifest.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <memory>
#include <utility>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/synchronization/lock.h"
#include "third_party/re2/src/re2/re2.h"

namespace views_shell {

ManifestProblem::ManifestProblem() = default;
ManifestProblem::ManifestProblem(std::vector<ManifestPathPart> path,
                                 std::string message)
    : path(std::move(path)), message(std::move(message)) {}
ManifestProblem::ManifestProblem(const ManifestProblem&) = default;
ManifestProblem::ManifestProblem(ManifestProblem&&) = default;
ManifestProblem& ManifestProblem::operator=(const ManifestProblem&) = default;
ManifestProblem& ManifestProblem::operator=(ManifestProblem&&) = default;
ManifestProblem::~ManifestProblem() = default;

std::string ManifestProblem::ToReason() const {
  if (path.empty()) {
    return base::StrCat({"(root): ", message});
  }
  std::vector<std::string> parts;
  for (const ManifestPathPart& part : path) {
    if (const std::string* key = std::get_if<std::string>(&part)) {
      parts.push_back(*key);
    } else {
      parts.push_back(base::NumberToString(std::get<size_t>(part)));
    }
  }
  return base::StrCat({base::JoinString(parts, "/"), ": ", message});
}

PluginCommandArg::PluginCommandArg() = default;
PluginCommandArg::PluginCommandArg(const PluginCommandArg&) = default;
PluginCommandArg::PluginCommandArg(PluginCommandArg&&) = default;
PluginCommandArg& PluginCommandArg::operator=(const PluginCommandArg&) =
    default;
PluginCommandArg& PluginCommandArg::operator=(PluginCommandArg&&) = default;
PluginCommandArg::~PluginCommandArg() = default;

PluginCommand::PluginCommand() = default;
PluginCommand::PluginCommand(PluginCommand&&) = default;
PluginCommand& PluginCommand::operator=(PluginCommand&&) = default;
PluginCommand::~PluginCommand() = default;

PluginSurface::PluginSurface() = default;
PluginSurface::PluginSurface(const PluginSurface&) = default;
PluginSurface::PluginSurface(PluginSurface&&) = default;
PluginSurface& PluginSurface::operator=(const PluginSurface&) = default;
PluginSurface& PluginSurface::operator=(PluginSurface&&) = default;
PluginSurface::~PluginSurface() = default;

PluginQuickSettingsEntry::PluginQuickSettingsEntry() = default;
PluginQuickSettingsEntry::PluginQuickSettingsEntry(
    const PluginQuickSettingsEntry&) = default;
PluginQuickSettingsEntry::PluginQuickSettingsEntry(PluginQuickSettingsEntry&&) =
    default;
PluginQuickSettingsEntry& PluginQuickSettingsEntry::operator=(
    const PluginQuickSettingsEntry&) = default;
PluginQuickSettingsEntry& PluginQuickSettingsEntry::operator=(
    PluginQuickSettingsEntry&&) = default;
PluginQuickSettingsEntry::~PluginQuickSettingsEntry() = default;

PluginManifest::PluginManifest() = default;
PluginManifest::PluginManifest(PluginManifest&&) = default;
PluginManifest& PluginManifest::operator=(PluginManifest&&) = default;
PluginManifest::~PluginManifest() = default;

namespace {

using Path = std::vector<ManifestPathPart>;

Path Sub(const Path& path, std::string_view key) {
  Path child = path;
  child.emplace_back(std::string(key));
  return child;
}

Path Sub(const Path& path, size_t index) {
  Path child = path;
  child.emplace_back(index);
  return child;
}

// --- Python repr ------------------------------------------------------------

std::string PyStrRepr(std::string_view text) {
  const bool has_single = text.find('\'') != std::string_view::npos;
  const bool has_double = text.find('"') != std::string_view::npos;
  const char quote = has_single && !has_double ? '"' : '\'';
  std::string out(1, quote);
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\\') {
      out += "\\\\";
    } else if (c == static_cast<unsigned char>(quote)) {
      out.push_back('\\');
      out.push_back(quote);
    } else if (c == '\t') {
      out += "\\t";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c < 0x20 || c == 0x7f) {
      out += base::StringPrintf("\\x%02x", c);
    } else if (c == 0xc2 && i + 1 < text.size() &&
               static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
               static_cast<unsigned char>(text[i + 1]) <= 0x9f) {
      // A C1 control character, which Python does not print raw.
      out += base::StringPrintf("\\x%02x",
                                static_cast<unsigned char>(text[i + 1]));
      ++i;
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  out.push_back(quote);
  return out;
}

std::string PyFloatRepr(double value) {
  if (std::isfinite(value) && value == std::floor(value) &&
      std::fabs(value) < 1e16) {
    return base::StringPrintf("%.1f", value);
  }
  return base::NumberToString(value);
}

std::string PyListOfStrings(std::initializer_list<std::string_view> items) {
  std::vector<std::string> reprs;
  for (std::string_view item : items) {
    reprs.push_back(PyStrRepr(item));
  }
  return base::StrCat({"[", base::JoinString(reprs, ", "), "]"});
}

// --- Patterns ---------------------------------------------------------------

const RE2& Regex(std::string_view source) {
  static base::NoDestructor<base::Lock> lock;
  static base::NoDestructor<std::map<std::string, std::unique_ptr<RE2>,
                                     std::less<>>>
      cache;
  base::AutoLock hold(*lock);
  auto it = cache->find(source);
  if (it == cache->end()) {
    it = cache->emplace(std::string(source), std::make_unique<RE2>(source))
             .first;
    CHECK(it->second->ok()) << "pattern does not compile: " << source;
  }
  return *it->second;
}

// Python's re.search: `$` also matches before one final newline.
bool PySearch(std::string_view source, std::string_view text) {
  const RE2& re = Regex(source);
  if (RE2::PartialMatch(text, re)) {
    return true;
  }
  return text.ends_with('\n') &&
         RE2::PartialMatch(text.substr(0, text.size() - 1), re);
}

constexpr char kRelativePathPattern[] =
    R"(^(?!/)(?!.*(^|/)\.\.(/|$))[A-Za-z0-9._/-]+$)";

// RE2 has no lookahead; the two (?!...) groups are checked by hand: no leading
// "/", and no ".." path component.
bool MatchesRelativePath(std::string_view text) {
  std::string_view body = text;
  if (body.ends_with('\n')) {
    body = body.substr(0, body.size() - 1);
  }
  if (body.starts_with('/')) {
    return false;
  }
  for (std::string_view part : base::SplitStringPiece(
           body, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (part == "..") {
      return false;
    }
  }
  return RE2::FullMatch(body, Regex("^[A-Za-z0-9._/-]+$"));
}

bool Matches(std::string_view source, std::string_view text) {
  if (source == kRelativePathPattern) {
    return MatchesRelativePath(text);
  }
  return PySearch(source, text);
}

constexpr char kPluginIdPattern[] =
    R"(^[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+$)";
constexpr char kVersionPattern[] =
    R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$)";
constexpr char kWildcardPattern[] = R"(^\s*\*\s*$)";
constexpr char kEnginePattern[] = R"(^[0-9<>=^~ .|-]+$)";
constexpr char kTargetPattern[] = R"(^//views_shell/)";
constexpr char kLocalIdPattern[] = R"(^[a-z][a-z0-9-]*$)";
constexpr char kCommandRefPattern[] =
    R"(^([a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+/)?[a-z][a-z0-9-]*$)";
constexpr char kCapabilityPattern[] =
    R"(^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$)";
constexpr char kWhenPattern[] = R"(^[A-Za-z0-9_.!&|=<>()'" -]+$)";
constexpr char kKeyPattern[] =
    R"(^((Mod|Super|Ctrl|Alt|Shift)\+)*([A-Za-z0-9]|F[0-9]{1,2}|Escape|Return|Space|Tab|BackSpace|Delete|Insert|Home|End|Prior|Next|Left|Right|Up|Down|Comma|Period|Slash|Semicolon|Minus|Equal|Grave|Print|XF86[A-Za-z0-9]+)$)";
constexpr char kProgramPattern[] = R"(^[A-Za-z0-9._+-]+$)";
constexpr char kOpenPattern[] = R"(^[a-z][a-z0-9/-]*$)";
constexpr char kAnchorPattern[] =
    R"(^(surface:[a-z][a-z0-9-]*|pointer|output-center)$)";
constexpr char kPropertyNamePattern[] = R"(^[a-z][A-Za-z0-9]*$)";
constexpr char kKeyFromPattern[] = R"(^config\.[a-z][A-Za-z0-9]*$)";
constexpr char kMenuLocationPattern[] =
    R"(^(views-shell\.menu(/[a-z][a-z0-9-]*)*|rail/workspace|rail/window|bar/context|tray/context)$)";
constexpr char kEventPattern[] =
    R"(^(workspace|window|output|mode|binding|reload|session\.(lock|unlock|idle|resume)|scroll\.(scroller|trails))$)";
constexpr char kSourceRefPattern[] =
    R"(^[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+/[a-z][a-z0-9-]*$)";
constexpr char kReservedIdPattern[] = R"(^views-shell\.)";
constexpr char kHttpsPattern[] = R"(^https://)";
constexpr char kQualifiedPattern[] =
    R"(^([a-z0-9][a-z0-9-]*(?:\.[a-z0-9][a-z0-9-]*)+)/([a-z][a-z0-9-]*)$)";

constexpr std::string_view kPermissionPatterns[] = {
    R"(^exec:[A-Za-z0-9._+-]+$)",
    R"(^path:(read|write):[~$A-Za-z0-9._/*{}-]+$)",
    R"(^network(:[A-Za-z0-9.-]+(:[0-9]+)?)?$)",
    R"(^dbus:(session|system):[A-Za-z0-9._-]+$)",
    R"(^compositor:[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$)",
    R"(^call:[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+/([a-z][a-z0-9-]*|\*)$)",
    R"(^state:read:[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+/[a-z][a-z0-9-]*$)",
};
constexpr std::string_view kPermissionWords[] = {
    "notifications", "media",      "clipboard:read",
    "clipboard:write", "scroll.lua", "scroll.lua.eval",
};

// --- Instance predicates ----------------------------------------------------

enum class JsonType { kObject, kArray, kString, kInteger, kNumber, kBoolean };

bool IsType(const base::Value& value, JsonType type) {
  switch (type) {
    case JsonType::kObject:
      return value.is_dict();
    case JsonType::kArray:
      return value.is_list();
    case JsonType::kString:
      return value.is_string();
    case JsonType::kInteger:
      // jsonschema counts 1.0 as an integer, and a boolean as neither.
      return value.is_int() ||
             (value.is_double() && std::isfinite(value.GetDouble()) &&
              value.GetDouble() == std::floor(value.GetDouble()));
    case JsonType::kNumber:
      return value.is_int() || value.is_double();
    case JsonType::kBoolean:
      return value.is_bool();
  }
}

std::string_view TypeName(JsonType type) {
  switch (type) {
    case JsonType::kObject:
      return "object";
    case JsonType::kArray:
      return "array";
    case JsonType::kString:
      return "string";
    case JsonType::kInteger:
      return "integer";
    case JsonType::kNumber:
      return "number";
    case JsonType::kBoolean:
      return "boolean";
  }
}

// `required` as a predicate: vacuously true for a non-object.
bool HasKey(const base::Value& value, std::string_view key) {
  return !value.is_dict() || value.GetDict().contains(key);
}

const base::Value* Member(const base::Value& value, std::string_view key) {
  return value.is_dict() ? value.GetDict().Find(key) : nullptr;
}

bool StringIs(const base::Value* value, std::string_view text) {
  return value && value->is_string() && value->GetString() == text;
}

// An `if: {properties: {key: {const|enum}}}` condition, vacuously true when
// the container is not an object or lacks the key.
bool IfOneOf(const base::Value* container,
             std::string_view key,
             std::initializer_list<std::string_view> accepted) {
  if (!container || !container->is_dict()) {
    return true;
  }
  const base::Value* value = container->GetDict().Find(key);
  if (!value) {
    return true;
  }
  for (std::string_view text : accepted) {
    if (StringIs(value, text)) {
      return true;
    }
  }
  return false;
}

double Number(const base::Value& value) {
  return value.is_int() ? value.GetInt() : value.GetDouble();
}

double CodePoints(const std::string& text) {
  return static_cast<double>(
      base::CountUnicodeCharacters(text).value_or(text.size()));
}

// --- The checker ------------------------------------------------------------

class SchemaChecker {
 public:
  std::vector<ManifestProblem> Take() {
    std::stable_sort(problems_.begin(), problems_.end(),
                     [](const ManifestProblem& a, const ManifestProblem& b) {
                       return PathLess(a.path, b.path);
                     });
    return std::move(problems_);
  }

  void CheckRoot(const base::Value& root);

 private:
  static bool PathLess(const Path& a, const Path& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
      if (a[i] == b[i]) {
        continue;
      }
      if (a[i].index() != b[i].index()) {
        return a[i].index() > b[i].index();  // an index before a key
      }
      return a[i] < b[i];
    }
    return a.size() < b.size();
  }

  void Fail(const Path& path, std::string message) {
    problems_.emplace_back(path, std::move(message));
  }

  // Keywords. Each ignores instances of a type it does not apply to, as
  // JSON Schema does.
  bool Type(const Path& path, const base::Value& value, JsonType type) {
    if (IsType(value, type)) {
      return true;
    }
    Fail(path, base::StrCat({PyRepr(value), " is not of type ",
                             PyStrRepr(TypeName(type))}));
    return false;
  }

  void Enum(const Path& path,
            const base::Value& value,
            std::initializer_list<std::string_view> options) {
    for (std::string_view option : options) {
      if (StringIs(&value, option)) {
        return;
      }
    }
    Fail(path, base::StrCat({PyRepr(value), " is not one of ",
                             PyListOfStrings(options)}));
  }

  void Pattern(const Path& path,
               const base::Value& value,
               std::string_view source) {
    if (value.is_string() && !Matches(source, value.GetString())) {
      Fail(path, base::StrCat({PyRepr(value), " does not match ",
                               PyStrRepr(source)}));
    }
  }

  void MinLength(const Path& path, const base::Value& value, size_t min) {
    if (value.is_string() && CodePoints(value.GetString()) < min) {
      Fail(path, base::StrCat({PyRepr(value), min == 1 ? " should be non-empty"
                                                       : " is too short"}));
    }
  }

  void MaxLength(const Path& path, const base::Value& value, size_t max) {
    if (value.is_string() && CodePoints(value.GetString()) > max) {
      Fail(path, base::StrCat({PyRepr(value), " is too long"}));
    }
  }

  void MinItems(const Path& path, const base::Value& value, size_t min) {
    if (value.is_list() && value.GetList().size() < min) {
      Fail(path, base::StrCat({PyRepr(value), min == 1 ? " should be non-empty"
                                                       : " is too short"}));
    }
  }

  void MaxItems(const Path& path, const base::Value& value, size_t max) {
    if (value.is_list() && value.GetList().size() > max) {
      Fail(path, base::StrCat({PyRepr(value), " is too long"}));
    }
  }

  void UniqueItems(const Path& path, const base::Value& value) {
    if (!value.is_list()) {
      return;
    }
    const base::ListValue& list = value.GetList();
    for (size_t i = 0; i < list.size(); ++i) {
      for (size_t j = i + 1; j < list.size(); ++j) {
        if (list[i] == list[j]) {
          Fail(path, base::StrCat({PyRepr(value), " has non-unique elements"}));
          return;
        }
      }
    }
  }

  void Minimum(const Path& path, const base::Value& value, double min) {
    if ((value.is_int() || value.is_double()) && Number(value) < min) {
      Fail(path, base::StrCat({PyRepr(value), " is less than the minimum of ",
                               base::NumberToString(static_cast<int>(min))}));
    }
  }

  void Maximum(const Path& path, const base::Value& value, double max) {
    if ((value.is_int() || value.is_double()) && Number(value) > max) {
      Fail(path,
           base::StrCat({PyRepr(value), " is greater than the maximum of ",
                         base::NumberToString(static_cast<int>(max))}));
    }
  }

  void ExclusiveMinimum(const Path& path, const base::Value& value, int min) {
    if ((value.is_int() || value.is_double()) && Number(value) <= min) {
      Fail(path, base::StrCat({PyRepr(value),
                               " is less than or equal to the minimum of ",
                               base::NumberToString(min)}));
    }
  }

  void Required(const Path& path,
                const base::Value& value,
                std::initializer_list<std::string_view> keys) {
    if (!value.is_dict()) {
      return;
    }
    for (std::string_view key : keys) {
      if (!value.GetDict().contains(key)) {
        Fail(path, base::StrCat({PyStrRepr(key), " is a required property"}));
      }
    }
  }

  void AdditionalProperties(const Path& path,
                            const base::Value& value,
                            std::initializer_list<std::string_view> allowed) {
    if (!value.is_dict()) {
      return;
    }
    std::vector<std::string> extras;
    for (const auto [key, unused] : value.GetDict()) {
      if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
        extras.push_back(PyStrRepr(key));
      }
    }
    if (extras.empty()) {
      return;
    }
    Fail(path, base::StrCat({"Additional properties are not allowed (",
                             base::JoinString(extras, ", "),
                             extras.size() == 1 ? " was" : " were",
                             " unexpected)"}));
  }

  void Not(const Path& path,
           const base::Value& value,
           bool subschema_valid,
           std::string_view subschema_repr) {
    if (subschema_valid) {
      Fail(path, base::StrCat({PyRepr(value), " should not be valid under ",
                               subschema_repr}));
    }
  }

  void AnyOfFailed(const Path& path, const base::Value& value) {
    Fail(path, base::StrCat({PyRepr(value),
                             " is not valid under any of the given schemas"}));
  }

  // Applies `check` to each property of an object that is present.
  template <typename Fn>
  void Prop(const Path& path,
            const base::Value& object,
            std::string_view key,
            Fn check) {
    if (const base::Value* value = Member(object, key)) {
      check(Sub(path, key), *value);
    }
  }

  template <typename Fn>
  void Items(const Path& path, const base::Value& value, Fn check) {
    if (!value.is_list()) {
      return;
    }
    const base::ListValue& list = value.GetList();
    for (size_t i = 0; i < list.size(); ++i) {
      check(Sub(path, i), list[i]);
    }
  }

  // --- $defs ---
  void String(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kString);
  }
  void Boolean(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kBoolean);
  }
  void Patterned(const Path& p, const base::Value& v, std::string_view src) {
    Type(p, v, JsonType::kString);
    Pattern(p, v, src);
  }
  void RelativePath(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kString);
    MinLength(p, v, 1);
    Pattern(p, v, kRelativePathPattern);
  }
  void LocalId(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kString);
    Pattern(p, v, kLocalIdPattern);
    MaxLength(p, v, 64);
  }
  void StringArray(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kArray);
    Items(p, v, [this](const Path& ip, const base::Value& iv) {
      String(ip, iv);
    });
  }
  void Permission(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kString);
    if (!v.is_string()) {
      return;  // every anyOf branch is vacuous for a non-string
    }
    for (std::string_view source : kPermissionPatterns) {
      if (Matches(source, v.GetString())) {
        return;
      }
    }
    for (std::string_view word : kPermissionWords) {
      if (v.GetString() == word) {
        return;
      }
    }
    AnyOfFailed(p, v);
  }
  void CapabilityArray(const Path& p, const base::Value& v) {
    Type(p, v, JsonType::kArray);
    UniqueItems(p, v);
    Items(p, v, [this](const Path& ip, const base::Value& iv) {
      Patterned(ip, iv, kCapabilityPattern);
    });
  }

  void CheckEngines(const Path& p, const base::Value& v);
  void CheckRuntime(const Path& p, const base::Value& v);
  void CheckRequires(const Path& p, const base::Value& v);
  void CheckChrome(const Path& p, const base::Value& v);
  void CheckContributes(const Path& p, const base::Value& v);
  void CheckCommand(const Path& p, const base::Value& v);
  void CheckSurface(const Path& p, const base::Value& v);
  void CheckConfiguration(const Path& p, const base::Value& v);
  void CheckKeybinding(const Path& p, const base::Value& v);
  void CheckQuickSettingsEntry(const Path& p, const base::Value& v);
  void CheckRootAllOf(const Path& p, const base::Value& root);

  std::vector<ManifestProblem> problems_;
};

void SchemaChecker::CheckRoot(const base::Value& root) {
  const Path p;
  if (!Type(p, root, JsonType::kObject)) {
    return;  // every other keyword is vacuous for a non-object
  }
  AdditionalProperties(
      p, root,
      {"$schema", "schemaVersion", "id", "name", "version", "description",
       "author", "license", "repository", "icon", "engines", "runtime",
       "permissions", "requires", "contributes", "chrome"});
  Required(p, root,
           {"schemaVersion", "id", "name", "version", "engines", "runtime"});
  Prop(p, root, "$schema",
       [this](const Path& q, const base::Value& v) { String(q, v); });
  Prop(p, root, "schemaVersion", [this](const Path& q, const base::Value& v) {
    // const 1: 1 and 1.0 equal it; true and "1" do not.
    if (v.is_bool() || !(v.is_int() || v.is_double()) || Number(v) != 1) {
      Fail(q, "1 was expected");
    }
  });
  Prop(p, root, "id", [this](const Path& q, const base::Value& v) {
    Type(q, v, JsonType::kString);
    Pattern(q, v, kPluginIdPattern);
    MaxLength(q, v, 128);
  });
  Prop(p, root, "name", [this](const Path& q, const base::Value& v) {
    Type(q, v, JsonType::kString);
    MinLength(q, v, 1);
    MaxLength(q, v, 64);
  });
  Prop(p, root, "version", [this](const Path& q, const base::Value& v) {
    Patterned(q, v, kVersionPattern);
  });
  Prop(p, root, "description", [this](const Path& q, const base::Value& v) {
    Type(q, v, JsonType::kString);
    MaxLength(q, v, 512);
  });
  for (std::string_view key : {"author", "license", "repository"}) {
    Prop(p, root, key,
         [this](const Path& q, const base::Value& v) { String(q, v); });
  }
  Prop(p, root, "icon",
       [this](const Path& q, const base::Value& v) { RelativePath(q, v); });
  Prop(p, root, "engines", [this](const Path& q, const base::Value& v) {
    CheckEngines(q, v);
  });
  Prop(p, root, "runtime", [this](const Path& q, const base::Value& v) {
    CheckRuntime(q, v);
  });
  Prop(p, root, "permissions", [this](const Path& q, const base::Value& v) {
    Type(q, v, JsonType::kArray);
    UniqueItems(q, v);
    Items(q, v, [this](const Path& ip, const base::Value& iv) {
      Permission(ip, iv);
    });
  });
  Prop(p, root, "requires", [this](const Path& q, const base::Value& v) {
    CheckRequires(q, v);
  });
  Prop(p, root, "contributes", [this](const Path& q, const base::Value& v) {
    CheckContributes(q, v);
  });
  Prop(p, root, "chrome", [this](const Path& q, const base::Value& v) {
    CheckChrome(q, v);
  });
  CheckRootAllOf(p, root);
}

void SchemaChecker::CheckEngines(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v, {"views-shell", "protocol"});
  Required(p, v, {"views-shell"});
  Prop(p, v, "views-shell", [this](const Path& q, const base::Value& e) {
    Type(q, e, JsonType::kString);
    MinLength(q, e, 1);
    Not(q, e, e.is_string() && Matches(kWildcardPattern, e.GetString()),
        R"({'pattern': '^\\s*\\*\\s*$'})");
    Pattern(q, e, kEnginePattern);
  });
  Prop(p, v, "protocol", [this](const Path& q, const base::Value& e) {
    Type(q, e, JsonType::kInteger);
    Minimum(q, e, 1);
  });
}

void SchemaChecker::CheckRuntime(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v, {"mode", "exec", "args", "target"});
  Required(p, v, {"mode"});
  Prop(p, v, "mode", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"builtin", "declarative", "process"});
  });
  Prop(p, v, "exec",
       [this](const Path& q, const base::Value& e) { RelativePath(q, e); });
  Prop(p, v, "args",
       [this](const Path& q, const base::Value& e) { StringArray(q, e); });
  Prop(p, v, "target", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kTargetPattern);
  });
  if (IfOneOf(&v, "mode", {"process"})) {
    Required(p, v, {"exec"});
    Not(p, v, HasKey(v, "target"), "{'required': ['target']}");
  }
  if (IfOneOf(&v, "mode", {"builtin"})) {
    Required(p, v, {"target"});
    Not(p, v, HasKey(v, "exec"), "{'required': ['exec']}");
  }
  if (IfOneOf(&v, "mode", {"declarative"})) {
    Not(p, v, HasKey(v, "exec") || HasKey(v, "target"),
        "{'anyOf': [{'required': ['exec']}, {'required': ['target']}]}");
  }
}

void SchemaChecker::CheckRequires(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v, {"compositor", "helpers", "plugins"});
  Prop(p, v, "compositor", [this](const Path& q, const base::Value& c) {
    Type(q, c, JsonType::kObject);
    AdditionalProperties(q, c, {"required", "optional"});
    Prop(q, c, "required", [this](const Path& r, const base::Value& e) {
      CapabilityArray(r, e);
    });
    Prop(q, c, "optional", [this](const Path& r, const base::Value& e) {
      CapabilityArray(r, e);
    });
  });
  Prop(p, v, "helpers", [this](const Path& q, const base::Value& h) {
    Type(q, h, JsonType::kArray);
    Items(q, h, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(ip, item, {"bin", "nixpkgs", "optional"});
      Required(ip, item, {"bin"});
      Prop(ip, item, "bin", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kProgramPattern);
      });
      Prop(ip, item, "nixpkgs",
           [this](const Path& r, const base::Value& e) { String(r, e); });
      Prop(ip, item, "optional",
           [this](const Path& r, const base::Value& e) { Boolean(r, e); });
    });
  });
  Prop(p, v, "plugins", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Patterned(ip, item, kPluginIdPattern);
    });
  });
}

void SchemaChecker::CheckChrome(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v, {"settingsPages", "pages"});
  Prop(p, v, "settingsPages", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(ip, item, {"id", "title", "path"});
      Required(ip, item, {"id", "title", "path"});
      Prop(ip, item, "id",
           [this](const Path& r, const base::Value& e) { LocalId(r, e); });
      Prop(ip, item, "title",
           [this](const Path& r, const base::Value& e) { String(r, e); });
      Prop(ip, item, "path", [this](const Path& r, const base::Value& e) {
        RelativePath(r, e);
      });
    });
  });
  Prop(p, v, "pages", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(ip, item,
                           {"id", "title", "url", "open", "placement"});
      Required(ip, item, {"id", "url"});
      Prop(ip, item, "id",
           [this](const Path& r, const base::Value& e) { LocalId(r, e); });
      Prop(ip, item, "title", [this](const Path& r, const base::Value& e) {
        Type(r, e, JsonType::kString);
        MaxLength(r, e, 48);
      });
      Prop(ip, item, "url", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kHttpsPattern);
      });
      Prop(ip, item, "open", [this](const Path& r, const base::Value& e) {
        Enum(r, e, {"tab", "app-window"});
      });
      Prop(ip, item, "placement", [this](const Path& r, const base::Value& pl) {
        Type(r, pl, JsonType::kObject);
        AdditionalProperties(r, pl,
                             {"workspace", "floating", "width", "height"});
        Prop(r, pl, "workspace", [this](const Path& s, const base::Value& e) {
          Enum(s, e, {"current", "scratchpad"});
        });
        Prop(r, pl, "floating",
             [this](const Path& s, const base::Value& e) { Boolean(s, e); });
        for (std::string_view key : {"width", "height"}) {
          Prop(r, pl, key, [this](const Path& s, const base::Value& e) {
            Type(s, e, JsonType::kNumber);
            ExclusiveMinimum(s, e, 0);
            Maximum(s, e, 1);
          });
        }
      });
    });
  });
}

void SchemaChecker::CheckContributes(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(
      p, v,
      {"commands", "surfaces", "configuration", "keybindings", "menus",
       "launcher", "cli", "compositor", "events", "sources", "quickSettings"});
  Prop(p, v, "commands", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      CheckCommand(ip, item);
    });
  });
  Prop(p, v, "surfaces", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      CheckSurface(ip, item);
    });
  });
  Prop(p, v, "configuration", [this](const Path& q, const base::Value& c) {
    CheckConfiguration(q, c);
  });
  Prop(p, v, "keybindings", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      CheckKeybinding(ip, item);
    });
  });
  Prop(p, v, "menus", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(ip, item, {"location", "command", "group", "when"});
      Required(ip, item, {"location", "command"});
      Prop(ip, item, "location", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kMenuLocationPattern);
      });
      Prop(ip, item, "command", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kCommandRefPattern);
      });
      Prop(ip, item, "group",
           [this](const Path& r, const base::Value& e) { String(r, e); });
      Prop(ip, item, "when", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kWhenPattern);
      });
    });
  });
  Prop(p, v, "launcher", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(
          ip, item, {"command", "title", "keywords", "icon", "desktopEntry"});
      Required(ip, item, {"command", "title"});
      Prop(ip, item, "command", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kCommandRefPattern);
      });
      Prop(ip, item, "title",
           [this](const Path& r, const base::Value& e) { String(r, e); });
      Prop(ip, item, "keywords", [this](const Path& r, const base::Value& e) {
        Type(r, e, JsonType::kArray);
        MaxItems(r, e, 30);
        Items(r, e, [this](const Path& s, const base::Value& w) {
          String(s, w);
        });
      });
      Prop(ip, item, "icon",
           [this](const Path& r, const base::Value& e) { String(r, e); });
      Prop(ip, item, "desktopEntry",
           [this](const Path& r, const base::Value& e) { Boolean(r, e); });
    });
  });
  Prop(p, v, "cli", [this](const Path& q, const base::Value& cli) {
    Type(q, cli, JsonType::kObject);
    AdditionalProperties(q, cli, {"name", "verbs"});
    Required(q, cli, {"name", "verbs"});
    Prop(q, cli, "name", [this](const Path& r, const base::Value& e) {
      Type(r, e, JsonType::kString);
      Pattern(r, e, kLocalIdPattern);
      bool reserved = false;
      for (std::string_view word : {"plugin", "call", "open", "bar", "help",
                                    "version", "gallery", "service"}) {
        reserved = reserved || StringIs(&e, word);
      }
      Not(r, e, reserved,
          "{'enum': ['plugin', 'call', 'open', 'bar', 'help', 'version', "
          "'gallery', 'service']}");
    });
    Prop(q, cli, "verbs", [this](const Path& r, const base::Value& verbs) {
      Type(r, verbs, JsonType::kArray);
      MinItems(r, verbs, 1);
      Items(r, verbs, [this](const Path& ip, const base::Value& item) {
        Type(ip, item, JsonType::kObject);
        AdditionalProperties(ip, item,
                             {"verb", "command", "summary", "confirm"});
        Required(ip, item, {"verb", "command"});
        Prop(ip, item, "verb", [this](const Path& s, const base::Value& e) {
          Patterned(s, e, kLocalIdPattern);
        });
        Prop(ip, item, "command", [this](const Path& s, const base::Value& e) {
          Patterned(s, e, kCommandRefPattern);
        });
        Prop(ip, item, "summary",
             [this](const Path& s, const base::Value& e) { String(s, e); });
        Prop(ip, item, "confirm",
             [this](const Path& s, const base::Value& e) { Boolean(s, e); });
      });
    });
  });
  Prop(p, v, "compositor", [this](const Path& q, const base::Value& c) {
    Type(q, c, JsonType::kObject);
    AdditionalProperties(q, c, {"scroll"});
    Prop(q, c, "scroll", [this](const Path& r, const base::Value& s) {
      Type(r, s, JsonType::kObject);
      AdditionalProperties(r, s, {"lua", "config"});
      for (std::string_view key : {"lua", "config"}) {
        Prop(r, s, key, [this](const Path& t, const base::Value& list) {
          Type(t, list, JsonType::kArray);
          Items(t, list, [this](const Path& ip, const base::Value& item) {
            RelativePath(ip, item);
          });
        });
      }
    });
  });
  Prop(p, v, "events", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    UniqueItems(q, list);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Patterned(ip, item, kEventPattern);
    });
  });
  Prop(p, v, "sources", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(ip, item, {"id", "description", "schema"});
      Required(ip, item, {"id"});
      Prop(ip, item, "id",
           [this](const Path& r, const base::Value& e) { LocalId(r, e); });
      Prop(ip, item, "description",
           [this](const Path& r, const base::Value& e) { String(r, e); });
      Prop(ip, item, "schema", [this](const Path& r, const base::Value& e) {
        RelativePath(r, e);
      });
    });
  });
  Prop(p, v, "quickSettings", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      CheckQuickSettingsEntry(ip, item);
    });
  });
}

// The handler oneOf: exactly one of four object shapes.
bool HandlerShapeValid(const base::Value& h) {
  if (!h.is_dict()) {
    return false;
  }
  const base::DictValue& d = h.GetDict();
  auto only = [&d](std::initializer_list<std::string_view> keys) {
    for (const auto [key, unused] : d) {
      if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        return false;
      }
    }
    return true;
  };
  const base::Value* args = d.Find("args");
  if (const base::Value* c = d.Find("compositor")) {
    return only({"compositor", "args"}) && c->is_string() &&
           Matches(kCapabilityPattern, c->GetString()) &&
           (!args || args->is_dict());
  }
  if (const base::Value* e = d.Find("exec")) {
    bool args_ok = !args;
    if (args && args->is_list()) {
      args_ok = std::ranges::all_of(
          args->GetList(), [](const base::Value& a) { return a.is_string(); });
    }
    return only({"exec", "args"}) && e->is_string() &&
           Matches(kProgramPattern, e->GetString()) && args_ok;
  }
  if (const base::Value* c = d.Find("call")) {
    return only({"call", "args"}) && c->is_string() &&
           Matches(kCommandRefPattern, c->GetString()) &&
           (!args || args->is_dict());
  }
  if (const base::Value* o = d.Find("open")) {
    return only({"open"}) && o->is_string() &&
           Matches(kOpenPattern, o->GetString());
  }
  return false;
}

void SchemaChecker::CheckCommand(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v,
                       {"id", "title", "category", "icon", "enablement",
                        "surface", "verb", "confirm", "result", "args",
                        "handler"});
  Required(p, v, {"id", "title"});
  Prop(p, v, "id",
       [this](const Path& q, const base::Value& e) { LocalId(q, e); });
  for (std::string_view key : {"title", "category", "icon"}) {
    Prop(p, v, key,
         [this](const Path& q, const base::Value& e) { String(q, e); });
  }
  Prop(p, v, "enablement", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kWhenPattern);
  });
  Prop(p, v, "surface",
       [this](const Path& q, const base::Value& e) { LocalId(q, e); });
  Prop(p, v, "verb", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"toggle", "show", "hide", "summon"});
  });
  Prop(p, v, "confirm",
       [this](const Path& q, const base::Value& e) { Boolean(q, e); });
  Prop(p, v, "result", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"none", "text", "json"});
  });
  Prop(p, v, "args", [this](const Path& q, const base::Value& list) {
    Type(q, list, JsonType::kArray);
    Items(q, list, [this](const Path& ip, const base::Value& item) {
      Type(ip, item, JsonType::kObject);
      AdditionalProperties(ip, item,
                           {"name", "type", "required", "description"});
      Required(ip, item, {"name", "type"});
      Prop(ip, item, "name", [this](const Path& r, const base::Value& e) {
        Patterned(r, e, kLocalIdPattern);
      });
      Prop(ip, item, "type", [this](const Path& r, const base::Value& e) {
        Enum(r, e, {"string", "integer", "number", "boolean"});
      });
      Prop(ip, item, "required",
           [this](const Path& r, const base::Value& e) { Boolean(r, e); });
      Prop(ip, item, "description",
           [this](const Path& r, const base::Value& e) { String(r, e); });
    });
  });
  Prop(p, v, "handler", [this](const Path& q, const base::Value& e) {
    if (!HandlerShapeValid(e)) {
      AnyOfFailed(q, e);  // oneOf with no match prints the anyOf message
    }
  });
}

void SchemaChecker::CheckSurface(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v,
                       {"id", "kind", "defaultSection", "allowMultiple",
                        "anchor", "keyboard", "ui", "prefix", "when"});
  Required(p, v, {"id", "kind"});
  Prop(p, v, "id",
       [this](const Path& q, const base::Value& e) { LocalId(q, e); });
  Prop(p, v, "kind", [this](const Path& q, const base::Value& e) {
    Enum(q, e,
         {"bar-widget", "bar", "panel", "overlay", "menu", "service",
          "picker-provider"});
  });
  Prop(p, v, "defaultSection", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"left", "center", "right"});
  });
  Prop(p, v, "allowMultiple",
       [this](const Path& q, const base::Value& e) { Boolean(q, e); });
  Prop(p, v, "anchor", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kAnchorPattern);
  });
  Prop(p, v, "keyboard", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"none", "on-demand", "exclusive"});
  });
  Prop(p, v, "ui",
       [this](const Path& q, const base::Value& e) { RelativePath(q, e); });
  Prop(p, v, "prefix", [this](const Path& q, const base::Value& e) {
    Type(q, e, JsonType::kString);
    MaxLength(q, e, 4);
  });
  Prop(p, v, "when", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kWhenPattern);
  });
}

void SchemaChecker::CheckConfiguration(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v, {"title", "properties"});
  Required(p, v, {"properties"});
  Prop(p, v, "title",
       [this](const Path& q, const base::Value& e) { String(q, e); });
  Prop(p, v, "properties", [this](const Path& q, const base::Value& props) {
    Type(q, props, JsonType::kObject);
    if (!props.is_dict()) {
      return;
    }
    for (const auto [name, unused] : props.GetDict()) {
      if (!Matches(kPropertyNamePattern, name)) {
        Fail(q, base::StrCat({PyStrRepr(name), " does not match ",
                              PyStrRepr(kPropertyNamePattern)}));
      }
    }
    for (const auto [name, prop] : props.GetDict()) {
      const Path r = Sub(q, name);
      Type(r, prop, JsonType::kObject);
      AdditionalProperties(r, prop,
                           {"type", "default", "description", "enum",
                            "enumDescriptions", "minimum", "maximum", "items",
                            "format", "scope", "order"});
      Required(r, prop, {"type"});
      Prop(r, prop, "type", [this](const Path& s, const base::Value& e) {
        Enum(s, e, {"string", "integer", "number", "boolean", "array"});
      });
      Prop(r, prop, "description",
           [this](const Path& s, const base::Value& e) { String(s, e); });
      Prop(r, prop, "enum", [this](const Path& s, const base::Value& e) {
        Type(s, e, JsonType::kArray);
      });
      Prop(r, prop, "enumDescriptions",
           [this](const Path& s, const base::Value& e) { StringArray(s, e); });
      for (std::string_view key : {"minimum", "maximum"}) {
        Prop(r, prop, key, [this](const Path& s, const base::Value& e) {
          Type(s, e, JsonType::kNumber);
        });
      }
      Prop(r, prop, "items", [this](const Path& s, const base::Value& e) {
        Type(s, e, JsonType::kObject);
      });
      Prop(r, prop, "format", [this](const Path& s, const base::Value& e) {
        Enum(s, e, {"keybinding", "path", "color-role", "command"});
      });
      Prop(r, prop, "scope", [this](const Path& s, const base::Value& e) {
        Enum(s, e, {"global", "instance"});
      });
      Prop(r, prop, "order", [this](const Path& s, const base::Value& e) {
        Type(s, e, JsonType::kInteger);
      });
    }
  });
}

void SchemaChecker::CheckKeybinding(const Path& p, const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v,
                       {"command", "key", "keyFrom", "args", "when",
                        "release", "locked", "mode"});
  Required(p, v, {"command"});
  if (!HasKey(v, "key") && !HasKey(v, "keyFrom")) {
    AnyOfFailed(p, v);
  }
  Prop(p, v, "command", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kCommandRefPattern);
  });
  Prop(p, v, "key", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kKeyPattern);
  });
  Prop(p, v, "keyFrom", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kKeyFromPattern);
  });
  Prop(p, v, "args", [this](const Path& q, const base::Value& e) {
    Type(q, e, JsonType::kObject);
  });
  Prop(p, v, "when", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kWhenPattern);
  });
  for (std::string_view key : {"release", "locked"}) {
    Prop(p, v, key,
         [this](const Path& q, const base::Value& e) { Boolean(q, e); });
  }
  Prop(p, v, "mode", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kLocalIdPattern);
  });
}

void SchemaChecker::CheckQuickSettingsEntry(const Path& p,
                                            const base::Value& v) {
  Type(p, v, JsonType::kObject);
  AdditionalProperties(p, v,
                       {"id", "slot", "ui", "state", "size", "page", "title",
                        "order", "when"});
  Required(p, v, {"id", "slot"});
  Prop(p, v, "id",
       [this](const Path& q, const base::Value& e) { LocalId(q, e); });
  Prop(p, v, "slot", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"tile", "slider", "card", "footer", "page"});
  });
  Prop(p, v, "ui",
       [this](const Path& q, const base::Value& e) { RelativePath(q, e); });
  Prop(p, v, "state", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kSourceRefPattern);
  });
  Prop(p, v, "size", [this](const Path& q, const base::Value& e) {
    Enum(q, e, {"primary", "compact"});
  });
  Prop(p, v, "page",
       [this](const Path& q, const base::Value& e) { LocalId(q, e); });
  Prop(p, v, "title", [this](const Path& q, const base::Value& e) {
    Type(q, e, JsonType::kString);
    MaxLength(q, e, 48);
  });
  Prop(p, v, "order", [this](const Path& q, const base::Value& e) {
    Type(q, e, JsonType::kInteger);
    Minimum(q, e, -1000);
    Maximum(q, e, 1000);
  });
  Prop(p, v, "when", [this](const Path& q, const base::Value& e) {
    Patterned(q, e, kWhenPattern);
  });
  if (IfOneOf(&v, "slot", {"page"})) {
    Required(p, v, {"title"});
    Not(p, v, HasKey(v, "page") || HasKey(v, "size"),
        "{'anyOf': [{'required': ['page']}, {'required': ['size']}]}");
  }
  if (IfOneOf(&v, "slot", {"slider", "card", "footer"})) {
    Not(p, v, HasKey(v, "size"), "{'required': ['size']}");
  }
  if (IfOneOf(&v, "slot", {"tile", "slider", "card", "footer"})) {
    Not(p, v, HasKey(v, "title"), "{'required': ['title']}");
  }
}

void SchemaChecker::CheckRootAllOf(const Path& p, const base::Value& root) {
  const base::Value* id = Member(root, "id");
  const base::Value* runtime = Member(root, "runtime");
  const base::Value* engines = Member(root, "engines");
  const base::Value* contributes = Member(root, "contributes");
  const base::Value* permissions = Member(root, "permissions");
  const base::Value* reqs = Member(root, "requires");

  // 0. views-shell.* ids are first-party: builtin or declarative.
  if (!id || !id->is_string() ||
      Matches(kReservedIdPattern, id->GetString())) {
    if (runtime) {
      if (const base::Value* mode = Member(*runtime, "mode")) {
        Enum(Sub(Sub(p, "runtime"), "mode"), *mode,
             {"builtin", "declarative"});
      }
    }
  }
  // 1. builtin is only for views-shell.* ids.
  if (IfOneOf(runtime, "mode", {"builtin"}) && id) {
    Pattern(Sub(p, "id"), *id, kReservedIdPattern);
  }
  // 2. process plugins declare the protocol version.
  if (IfOneOf(runtime, "mode", {"process"}) && engines) {
    Required(Sub(p, "engines"), *engines, {"views-shell", "protocol"});
  }
  // 3. scroll Lua needs the scroll.lua capability and permission.
  bool ships_lua = false;
  if (contributes) {
    ships_lua = true;
    if (contributes->is_dict()) {
      const base::Value* compositor = Member(*contributes, "compositor");
      ships_lua = compositor != nullptr;
      if (compositor && compositor->is_dict()) {
        const base::Value* scroll = Member(*compositor, "scroll");
        ships_lua = scroll && HasKey(*scroll, "lua");
      }
    }
  }
  if (ships_lua) {
    Required(p, root, {"permissions", "requires"});
    if (permissions && permissions->is_list() &&
        !std::ranges::any_of(permissions->GetList(),
                             [](const base::Value& e) {
                               return StringIs(&e, "scroll.lua");
                             })) {
      Fail(Sub(p, "permissions"),
           base::StrCat({PyRepr(*permissions),
                         " does not contain items matching the given schema"}));
    }
    if (reqs) {
      Required(Sub(p, "requires"), *reqs, {"compositor"});
      if (const base::Value* compositor = Member(*reqs, "compositor");
          compositor && compositor->is_dict()) {
        auto lists_lua = [compositor](std::string_view key) {
          const base::Value* list = Member(*compositor, key);
          if (!list) {
            return false;
          }
          return !list->is_list() ||
                 std::ranges::any_of(list->GetList(), [](const base::Value& e) {
                   return StringIs(&e, "scroll.lua");
                 });
        };
        if (!lists_lua("required") && !lists_lua("optional")) {
          AnyOfFailed(Sub(Sub(p, "requires"), "compositor"), *compositor);
        }
      }
    }
  }
  const base::Value* commands =
      contributes ? Member(*contributes, "commands") : nullptr;
  const base::Value* quick =
      contributes ? Member(*contributes, "quickSettings") : nullptr;
  const Path commands_path = Sub(Sub(p, "contributes"), "commands");
  const Path quick_path = Sub(Sub(p, "contributes"), "quickSettings");
  // 4. only declarative plugins carry command handlers.
  if (IfOneOf(runtime, "mode", {"builtin", "process"}) && commands) {
    Items(commands_path, *commands,
          [this](const Path& q, const base::Value& item) {
            Not(q, item, HasKey(item, "handler"),
                "{'required': ['handler']}");
          });
  }
  // 5. declarative plugins publish no sources.
  if (IfOneOf(runtime, "mode", {"declarative"}) && contributes) {
    Not(Sub(p, "contributes"), *contributes, HasKey(*contributes, "sources"),
        "{'required': ['sources']}");
  }
  // 6. process plugins send quick-settings trees at runtime.
  if (IfOneOf(runtime, "mode", {"process"}) && quick) {
    Items(quick_path, *quick, [this](const Path& q, const base::Value& item) {
      Not(q, item, HasKey(item, "ui"), "{'required': ['ui']}");
    });
  }
  // 7. builtin and declarative entries name their ui file.
  if (IfOneOf(runtime, "mode", {"builtin", "declarative"}) && quick) {
    Items(quick_path, *quick, [this](const Path& q, const base::Value& item) {
      Required(q, item, {"ui"});
    });
  }
}

const std::string* FindString(const base::DictValue& dict,
                              std::string_view key) {
  return dict.FindString(key);
}

std::optional<std::string> OptionalString(const base::DictValue& dict,
                                          std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? std::optional<std::string>(*value) : std::nullopt;
}

const base::ListValue& ListOrEmpty(const base::DictValue* dict,
                                   std::string_view key) {
  static const base::NoDestructor<base::ListValue> empty;
  const base::ListValue* list = dict ? dict->FindList(key) : nullptr;
  return list ? *list : *empty;
}

}  // namespace

std::string PyRepr(const base::Value& value) {
  switch (value.type()) {
    case base::Value::Type::NONE:
      return "None";
    case base::Value::Type::BOOLEAN:
      return value.GetBool() ? "True" : "False";
    case base::Value::Type::INTEGER:
      return base::NumberToString(value.GetInt());
    case base::Value::Type::DOUBLE:
      return PyFloatRepr(value.GetDouble());
    case base::Value::Type::STRING:
      return PyStrRepr(value.GetString());
    case base::Value::Type::BINARY:
      return "b''";
    case base::Value::Type::LIST: {
      std::vector<std::string> items;
      for (const base::Value& item : value.GetList()) {
        items.push_back(PyRepr(item));
      }
      return base::StrCat({"[", base::JoinString(items, ", "), "]"});
    }
    case base::Value::Type::DICT: {
      std::vector<std::string> items;
      for (const auto [key, item] : value.GetDict()) {
        items.push_back(base::StrCat({PyStrRepr(key), ": ", PyRepr(item)}));
      }
      return base::StrCat({"{", base::JoinString(items, ", "), "}"});
    }
  }
}

std::vector<ManifestProblem> CheckManifestSchema(const base::Value& manifest) {
  SchemaChecker checker;
  checker.CheckRoot(manifest);
  return checker.Take();
}

void CollectCommandReferences(const base::Value& node,
                              std::vector<std::string>* out) {
  if (const base::DictValue* dict = node.GetIfDict()) {
    for (const auto [key, value] : *dict) {
      if ((key == "command" || key == "call") && value.is_string()) {
        out->push_back(value.GetString());
      } else {
        CollectCommandReferences(value, out);
      }
    }
  } else if (const base::ListValue* list = node.GetIfList()) {
    for (const base::Value& item : *list) {
      CollectCommandReferences(item, out);
    }
  }
}

bool CommandReferenceCovered(std::string_view plugin_id,
                             const base::flat_set<std::string>& permissions,
                             std::string_view command) {
  std::string owner;
  if (!RE2::FullMatch(command, Regex(kQualifiedPattern), &owner) ||
      owner == plugin_id) {
    return true;
  }
  return permissions.contains(base::StrCat({"call:", owner, "/*"})) ||
         permissions.contains(base::StrCat({"call:", command}));
}

std::vector<std::string> CheckManifestFiles(const base::DictValue& manifest,
                                            const base::FilePath& dir) {
  std::vector<std::string> failures;
  std::vector<std::string> named;
  const base::DictValue* runtime = manifest.FindDict("runtime");
  if (const std::string* exec = runtime ? runtime->FindString("exec") : nullptr) {
    named.push_back(*exec);
  }
  const base::DictValue* c = manifest.FindDict("contributes");
  for (std::string_view list : {"surfaces", "quickSettings"}) {
    for (const base::Value& item : ListOrEmpty(c, list)) {
      if (const std::string* ui = item.GetDict().FindString("ui")) {
        named.push_back(*ui);
      }
    }
  }
  for (const base::Value& item : ListOrEmpty(c, "sources")) {
    if (const std::string* schema = item.GetDict().FindString("schema")) {
      named.push_back(*schema);
    }
  }
  const base::DictValue* chrome = manifest.FindDict("chrome");
  for (const base::Value& item : ListOrEmpty(chrome, "settingsPages")) {
    named.push_back(*item.GetDict().FindString("path"));
  }
  const base::DictValue* compositor = c ? c->FindDict("compositor") : nullptr;
  const base::DictValue* scroll =
      compositor ? compositor->FindDict("scroll") : nullptr;
  for (std::string_view list : {"lua", "config"}) {
    for (const base::Value& item : ListOrEmpty(scroll, list)) {
      named.push_back(item.GetString());
    }
  }
  for (const std::string& name : named) {
    if (!base::PathExists(dir.AppendASCII(name)) ||
        base::DirectoryExists(dir.AppendASCII(name))) {
      failures.push_back(base::StrCat({"names missing file ", name}));
    }
  }

  std::vector<std::string> ids;
  for (const base::Value& command : ListOrEmpty(c, "commands")) {
    ids.push_back(*command.GetDict().FindString("id"));
  }
  std::vector<std::string> refs;
  for (std::string_view list : {"keybindings", "menus", "launcher"}) {
    for (const base::Value& item : ListOrEmpty(c, list)) {
      refs.push_back(*item.GetDict().FindString("command"));
    }
  }
  const base::DictValue* cli = c ? c->FindDict("cli") : nullptr;
  for (const base::Value& verb : ListOrEmpty(cli, "verbs")) {
    refs.push_back(*verb.GetDict().FindString("command"));
  }
  for (const std::string& ref : refs) {
    if (ref.find('/') == std::string::npos &&
        std::ranges::find(ids, ref) == ids.end()) {
      failures.push_back(base::StrCat({"references undeclared command ", ref}));
    }
  }

  const base::ListValue& quick = ListOrEmpty(c, "quickSettings");
  std::vector<std::string> pages;
  for (const base::Value& entry : quick) {
    if (StringIs(entry.GetDict().Find("slot"), "page")) {
      pages.push_back(*entry.GetDict().FindString("id"));
    }
  }
  for (const base::Value& entry : quick) {
    const std::string* page = entry.GetDict().FindString("page");
    if (page && std::ranges::find(pages, *page) == pages.end()) {
      failures.push_back(base::StrCat({"entry ",
                                       *entry.GetDict().FindString("id"),
                                       " opens undeclared page ", *page}));
    }
  }

  base::flat_set<std::string> permissions;
  for (const base::Value& permission : ListOrEmpty(&manifest, "permissions")) {
    permissions.insert(permission.GetString());
  }
  std::vector<std::string> calls;
  for (const base::Value& command : ListOrEmpty(c, "commands")) {
    if (const base::DictValue* handler = command.GetDict().FindDict("handler")) {
      CollectCommandReferences(base::Value(handler->Clone()), &calls);
    }
  }
  for (const std::string& name : named) {
    const base::FilePath file = dir.AppendASCII(name);
    std::string text;
    if (!name.ends_with(".json") || !base::ReadFileToString(file, &text)) {
      continue;
    }
    if (std::optional<base::Value> tree =
            base::JSONReader::Read(text, base::JSON_PARSE_RFC)) {
      CollectCommandReferences(*tree, &calls);
    }
  }
  const std::string* id = manifest.FindString("id");
  for (const std::string& call : calls) {
    if (!CommandReferenceCovered(id ? *id : std::string(), permissions,
                                 call)) {
      failures.push_back(
          base::StrCat({"calls ", call, " without a call: permission"}));
    }
  }
  return failures;
}

std::string_view PluginTierName(PluginTier tier) {
  switch (tier) {
    case PluginTier::kBuiltin:
      return "builtin";
    case PluginTier::kDeclarative:
      return "declarative";
    case PluginTier::kProcess:
      return "process";
  }
}

const PluginCommand* PluginManifest::FindCommand(
    std::string_view command) const {
  std::string_view local = command;
  if (const size_t slash = command.find('/'); slash != std::string_view::npos) {
    if (command.substr(0, slash) != id) {
      return nullptr;
    }
    local = command.substr(slash + 1);
  }
  for (const PluginCommand& candidate : commands) {
    if (candidate.id == local) {
      return &candidate;
    }
  }
  return nullptr;
}

bool PluginManifest::HasPermission(std::string_view permission) const {
  return permissions.contains(permission);
}

std::vector<std::string> PluginManifest::TreeSurfaces() const {
  std::vector<std::string> out;
  for (const PluginSurface& surface : surfaces) {
    if (surface.kind != "service" && surface.kind != "picker-provider") {
      out.push_back(surface.id);
    }
  }
  for (const PluginQuickSettingsEntry& entry : quick_settings) {
    out.push_back(entry.id);
  }
  return out;
}

std::string PluginManifest::Qualify(std::string_view reference) const {
  if (reference.find('/') != std::string_view::npos) {
    return std::string(reference);
  }
  return base::StrCat({id, "/", reference});
}

PluginManifest BuildPluginManifest(base::DictValue manifest,
                                   const base::FilePath& dir) {
  PluginManifest m;
  m.dir = dir;
  m.id = *FindString(manifest, "id");
  m.name = *FindString(manifest, "name");
  m.version = *FindString(manifest, "version");
  const base::DictValue& runtime = *manifest.FindDict("runtime");
  const std::string& mode = *runtime.FindString("mode");
  m.tier = mode == "builtin"       ? PluginTier::kBuiltin
           : mode == "declarative" ? PluginTier::kDeclarative
                                   : PluginTier::kProcess;
  if (const base::DictValue* engines = manifest.FindDict("engines")) {
    if (const base::Value* protocol = engines->Find("protocol")) {
      m.protocol = static_cast<int>(Number(*protocol));
    }
  }
  m.exec = OptionalString(runtime, "exec").value_or(std::string());
  m.target = OptionalString(runtime, "target").value_or(std::string());
  for (const base::Value& arg : ListOrEmpty(&runtime, "args")) {
    m.exec_args.push_back(arg.GetString());
  }
  for (const base::Value& permission : ListOrEmpty(&manifest, "permissions")) {
    m.permissions.insert(permission.GetString());
  }
  const base::DictValue* reqs = manifest.FindDict("requires");
  const base::DictValue* compositor =
      reqs ? reqs->FindDict("compositor") : nullptr;
  for (const base::Value& cap : ListOrEmpty(compositor, "required")) {
    m.required_capabilities.insert(cap.GetString());
  }
  for (const base::Value& cap : ListOrEmpty(compositor, "optional")) {
    m.optional_capabilities.insert(cap.GetString());
  }
  for (const base::Value& plugin : ListOrEmpty(reqs, "plugins")) {
    m.dependencies.push_back(plugin.GetString());
  }

  const base::DictValue* c = manifest.FindDict("contributes");
  const base::DictValue* configuration =
      c ? c->FindDict("configuration") : nullptr;
  const base::DictValue* properties =
      configuration ? configuration->FindDict("properties") : nullptr;
  if (properties) {
    for (const auto [name, prop] : *properties) {
      if (const base::Value* value = prop.GetDict().Find("default")) {
        m.config_defaults.Set(name, value->Clone());
      }
    }
  }
  for (const base::Value& item : ListOrEmpty(c, "commands")) {
    const base::DictValue& d = item.GetDict();
    PluginCommand command;
    command.id = *d.FindString("id");
    command.title = *d.FindString("title");
    command.result = OptionalString(d, "result").value_or("none");
    command.surface = OptionalString(d, "surface");
    command.verb = OptionalString(d, "verb");
    command.confirm = d.FindBool("confirm").value_or(false);
    for (const base::Value& arg : ListOrEmpty(&d, "args")) {
      PluginCommandArg a;
      a.name = *arg.GetDict().FindString("name");
      a.type = *arg.GetDict().FindString("type");
      a.required = arg.GetDict().FindBool("required").value_or(false);
      command.args.push_back(std::move(a));
    }
    if (const base::Value* handler = d.Find("handler")) {
      command.handler = handler->Clone();
    }
    m.commands.push_back(std::move(command));
  }
  for (const base::Value& item : ListOrEmpty(c, "surfaces")) {
    PluginSurface surface;
    surface.id = *item.GetDict().FindString("id");
    surface.kind = *item.GetDict().FindString("kind");
    surface.ui = OptionalString(item.GetDict(), "ui");
    m.surfaces.push_back(std::move(surface));
  }
  for (const base::Value& item : ListOrEmpty(c, "quickSettings")) {
    PluginQuickSettingsEntry entry;
    entry.id = *item.GetDict().FindString("id");
    entry.slot = *item.GetDict().FindString("slot");
    entry.ui = OptionalString(item.GetDict(), "ui");
    entry.state = OptionalString(item.GetDict(), "state");
    m.quick_settings.push_back(std::move(entry));
  }
  for (const base::Value& event : ListOrEmpty(c, "events")) {
    m.events.push_back(event.GetString());
  }
  for (const base::Value& source : ListOrEmpty(c, "sources")) {
    m.sources.push_back(*source.GetDict().FindString("id"));
  }
  if (const base::DictValue* cli = c ? c->FindDict("cli") : nullptr) {
    m.cli_name = *cli->FindString("name");
  }
  for (const base::Value& item : ListOrEmpty(c, "keybindings")) {
    const base::DictValue& d = item.GetDict();
    if (const std::string* key = d.FindString("key")) {
      m.keybinding_keys.push_back(*key);
    } else if (const std::string* from = d.FindString("keyFrom")) {
      const std::string name = from->substr(from->find('.') + 1);
      const base::DictValue* prop =
          properties ? properties->FindDict(name) : nullptr;
      const std::string* fallback =
          prop ? prop->FindString("default") : nullptr;
      if (fallback) {
        m.keybinding_keys.push_back(*fallback);
      }
    }
  }
  m.raw = std::move(manifest);
  return m;
}

base::expected<PluginManifest, std::string> LoadPluginManifest(
    const base::FilePath& dir_or_manifest) {
  base::FilePath file = dir_or_manifest;
  if (base::DirectoryExists(file)) {
    file = file.AppendASCII(kPluginManifestFileName);
  }
  file = base::MakeAbsoluteFilePath(file);
  if (file.empty()) {
    return base::unexpected(base::StrCat(
        {"no ", kPluginManifestFileName, " at ", dir_or_manifest.value()}));
  }
  std::string text;
  if (!base::ReadFileToString(file, &text)) {
    return base::unexpected(base::StrCat({"cannot read ", file.value()}));
  }
  auto parsed =
      base::JSONReader::ReadAndReturnValueWithError(text, base::JSON_PARSE_RFC);
  if (!parsed.has_value()) {
    return base::unexpected(
        base::StrCat({"not JSON: ", parsed.error().message}));
  }
  std::vector<ManifestProblem> problems = CheckManifestSchema(*parsed);
  if (!problems.empty()) {
    return base::unexpected(problems.front().ToReason());
  }
  const base::FilePath dir = file.DirName();
  std::vector<std::string> failures =
      CheckManifestFiles(parsed->GetDict(), dir);
  if (!failures.empty()) {
    return base::unexpected(failures.front());
  }
  return BuildPluginManifest(std::move(parsed->GetDict()), dir);
}

}  // namespace views_shell
