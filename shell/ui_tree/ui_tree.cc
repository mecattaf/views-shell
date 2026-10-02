// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/ui_tree/ui_tree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace views_shell::ui_tree {
namespace {

// What a property may hold (schemas/ui-tree.schema.json $defs).
enum class Kind {
  kText,         // a string or a binding
  kBool,         // a boolean or a binding
  kNumber,       // a number or a binding
  kIcon,         // an icon name (^(xdg:)?[A-Za-z0-9_.-]+$) or a binding
  kEnum,         // one of `values`
  kInteger,      // an integer in [min, max]
  kAction,       // an action object
  kBinding,      // a binding only
  kId,           // ^[a-z][a-z0-9-]*$
  kString,       // any string
  kBoolLiteral,  // a boolean, never bound
  kOptions,      // a binding or an array of options
  kOptionList,   // an array of 2 to 5 options, never bound
  kNode,         // one node
  kChildren,     // an array of nodes
};

struct PropSpec {
  std::string_view name;
  Kind kind;
  bool required = false;
  std::vector<std::string_view> values = {};
  int min = 0;
  int max = 0;
};

struct NodeSpec {
  NodeType type;
  std::string_view name;
  std::vector<PropSpec> props;
};

const std::vector<std::string_view>& Spacing() {
  static const base::NoDestructor<std::vector<std::string_view>> kValues(
      {"none", "tight", "normal", "loose"});
  return *kValues;
}
const std::vector<std::string_view>& Roles() {
  static const base::NoDestructor<std::vector<std::string_view>> kValues(
      {"default", "subtle", "primary", "positive", "warning", "alert",
       "disabled"});
  return *kValues;
}
const std::vector<std::string_view>& Typography() {
  static const base::NoDestructor<std::vector<std::string_view>> kValues(
      {"display", "title", "headline", "body", "body-strong", "button",
       "annotation", "label"});
  return *kValues;
}
const std::vector<std::string_view>& Align() {
  static const base::NoDestructor<std::vector<std::string_view>> kValues(
      {"start", "center", "end", "stretch"});
  return *kValues;
}
const std::vector<std::string_view>& Container() {
  static const base::NoDestructor<std::vector<std::string_view>> kValues(
      {"none", "rounded", "rounded-top", "rounded-bottom"});
  return *kValues;
}

// The schema's node definitions. The common properties ($defs/common) are
// added to every node by Spec().
std::vector<NodeSpec> BuildSpecs() {
  std::vector<NodeSpec> specs;
  auto box = [](NodeType type, std::string_view name) {
    return NodeSpec{type,
                    name,
                    {{"spacing", Kind::kEnum, false, Spacing()},
                     {"align", Kind::kEnum, false, Align()},
                     {"container", Kind::kEnum, false, Container()},
                     {"children", Kind::kChildren}}};
  };
  specs.push_back(box(NodeType::kColumn, "column"));
  specs.push_back(box(NodeType::kRow, "row"));
  specs.push_back({NodeType::kStack, "stack", {{"children", Kind::kChildren}}});
  specs.push_back({NodeType::kGrid,
                   "grid",
                   {{"columns", Kind::kInteger, true, {}, 1, 6},
                    {"spacing", Kind::kEnum, false, Spacing()},
                    {"children", Kind::kChildren}}});
  specs.push_back({NodeType::kScroll,
                   "scroll",
                   {{"maxHeight",
                     Kind::kEnum,
                     false,
                     {"small", "medium", "large", "surface"}},
                    {"child", Kind::kNode, true}}});
  specs.push_back({NodeType::kRepeat,
                   "repeat",
                   {{"items", Kind::kBinding, true},
                    {"key", Kind::kString},
                    {"direction", Kind::kEnum, false, {"column", "row"}},
                    {"template", Kind::kNode, true}}});
  specs.push_back(
      {NodeType::kLabel,
       "label",
       {{"text", Kind::kText, true},
        {"typography", Kind::kEnum, false, Typography()},
        {"role", Kind::kEnum, false, Roles()},
        {"elide", Kind::kEnum, false, {"tail", "middle", "head", "none"}},
        {"maxLines", Kind::kInteger, false, {}, 1, 20}}});
  specs.push_back(
      {NodeType::kMarkdown, "markdown", {{"text", Kind::kText, true}}});
  specs.push_back({NodeType::kIcon,
                   "icon",
                   {{"icon", Kind::kIcon, true},
                    {"size", Kind::kEnum, false, {"small", "medium", "large"}},
                    {"role", Kind::kEnum, false, Roles()}}});
  specs.push_back(
      {NodeType::kImage,
       "image",
       {{"src", Kind::kText, true},
        {"size", Kind::kEnum, false, {"thumbnail", "small", "medium", "large"}},
        {"rounded", Kind::kBoolLiteral}}});
  specs.push_back(
      {NodeType::kBadge,
       "badge",
       {{"text", Kind::kText, true}, {"role", Kind::kEnum, false, Roles()}}});
  specs.push_back(
      {NodeType::kDot, "dot", {{"role", Kind::kEnum, false, Roles()}}});
  specs.push_back({NodeType::kSeparator, "separator", {}});
  specs.push_back(
      {NodeType::kSpacer, "spacer", {{"size", Kind::kEnum, false, Spacing()}}});
  specs.push_back({NodeType::kProgress,
                   "progress",
                   {{"value", Kind::kNumber},
                    {"shape", Kind::kEnum, false, {"bar", "ring"}}}});
  specs.push_back(
      {NodeType::kButton,
       "button",
       {{"label", Kind::kText, true},
        {"icon", Kind::kIcon},
        {"variant",
         Kind::kEnum,
         false,
         {"default", "primary", "secondary", "floating", "alert", "accent"}},
        {"action", Kind::kAction, true}}});
  specs.push_back(
      {NodeType::kIconButton,
       "iconButton",
       {{"icon", Kind::kIcon, true},
        {"size",
         Kind::kEnum,
         false,
         {"xsmall", "small", "medium", "large", "xlarge"}},
        {"variant", Kind::kEnum, false, {"default", "prominent", "floating"}},
        {"toggled", Kind::kBool},
        {"action", Kind::kAction, true},
        {"accessibleName", Kind::kText, true}}});
  specs.push_back({NodeType::kSwitch,
                   "switch",
                   {{"label", Kind::kText},
                    {"value", Kind::kBool, true},
                    {"action", Kind::kAction, true}}});
  specs.push_back({NodeType::kCheckbox,
                   "checkbox",
                   {{"label", Kind::kText, true},
                    {"value", Kind::kBool, true},
                    {"action", Kind::kAction, true}}});
  specs.push_back({NodeType::kRadioGroup,
                   "radioGroup",
                   {{"options", Kind::kOptions, true},
                    {"value", Kind::kText, true},
                    {"action", Kind::kAction, true}}});
  specs.push_back({NodeType::kSelect,
                   "select",
                   {{"label", Kind::kText},
                    {"options", Kind::kOptions, true},
                    {"value", Kind::kText, true},
                    {"action", Kind::kAction, true}}});
  specs.push_back({NodeType::kSlider,
                   "slider",
                   {{"icon", Kind::kIcon},
                    {"value", Kind::kNumber, true},
                    {"action", Kind::kAction, true},
                    {"toggled", Kind::kBool},
                    {"iconAction", Kind::kAction}}});
  specs.push_back({NodeType::kTextfield,
                   "textfield",
                   {{"placeholder", Kind::kText},
                    {"value", Kind::kText},
                    {"size", Kind::kEnum, false, {"small", "medium", "large"}},
                    {"action", Kind::kAction, true}}});
  specs.push_back({NodeType::kListItem,
                   "listItem",
                   {{"icon", Kind::kIcon},
                    {"label", Kind::kText, true},
                    {"sublabel", Kind::kText},
                    {"selected", Kind::kBool},
                    {"trailing", Kind::kNode},
                    {"action", Kind::kAction}}});
  specs.push_back({NodeType::kTile,
                   "tile",
                   {{"variant", Kind::kEnum, false, {"primary", "compact"}},
                    {"icon", Kind::kIcon, true},
                    {"label", Kind::kText, true},
                    {"sublabel", Kind::kText},
                    {"toggled", Kind::kBool},
                    {"action", Kind::kAction},
                    {"detail", Kind::kId}}});
  specs.push_back({NodeType::kTabSlider,
                   "tabSlider",
                   {{"options", Kind::kOptionList, true},
                    {"value", Kind::kText, true},
                    {"action", Kind::kAction, true}}});
  specs.push_back(
      {NodeType::kKeyChips, "keyChips", {{"keys", Kind::kText, true}}});
  specs.push_back(
      {NodeType::kMediaSession,
       "mediaSession",
       {{"session", Kind::kBinding, true}, {"action", Kind::kAction}}});

  // $defs/common, on every node. A node that also lists one (iconButton's
  // required accessibleName) keeps its own entry.
  const std::vector<PropSpec> common = {
      {"id", Kind::kId},
      {"visible", Kind::kBool},
      {"enabled", Kind::kBool},
      {"tooltip", Kind::kText},
      {"accessibleName", Kind::kText},
      {"flex", Kind::kInteger, false, {}, 0, 10},
  };
  for (NodeSpec& spec : specs) {
    for (const PropSpec& c : common) {
      const bool listed = std::ranges::any_of(
          spec.props, [&c](const PropSpec& p) { return p.name == c.name; });
      if (!listed) {
        spec.props.push_back(c);
      }
    }
  }
  return specs;
}

const std::vector<NodeSpec>& Specs() {
  static const base::NoDestructor<std::vector<NodeSpec>> kSpecs(BuildSpecs());
  return *kSpecs;
}

const NodeSpec* FindSpec(std::string_view name) {
  for (const NodeSpec& spec : Specs()) {
    if (spec.name == name) {
      return &spec;
    }
  }
  return nullptr;
}

const NodeSpec* FindSpec(NodeType type) {
  for (const NodeSpec& spec : Specs()) {
    if (spec.type == type) {
      return &spec;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------- patterns

bool IsLowerDigit(char c) {
  return base::IsAsciiLower(c) || base::IsAsciiDigit(c);
}

// ^[a-z][a-z0-9-]*$
bool IsLowerId(std::string_view s) {
  if (s.empty() || !base::IsAsciiLower(s.front())) {
    return false;
  }
  return std::ranges::all_of(
      s, [](char c) { return IsLowerDigit(c) || c == '-'; });
}

// ^[a-z0-9][a-z0-9-]*$
bool IsPluginIdPart(std::string_view s) {
  if (s.empty() || !IsLowerDigit(s.front())) {
    return false;
  }
  return std::ranges::all_of(
      s, [](char c) { return IsLowerDigit(c) || c == '-'; });
}

// ^[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+$
bool IsPluginId(std::string_view s) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      s, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  return parts.size() >= 2 && std::ranges::all_of(parts, &IsPluginIdPart);
}

// ^([a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+/)?[a-z][a-z0-9-]*$
bool IsCommandName(std::string_view s) {
  const size_t slash = s.find('/');
  if (slash == std::string_view::npos) {
    return IsLowerId(s);
  }
  return IsPluginId(s.substr(0, slash)) && IsLowerId(s.substr(slash + 1));
}

// ^[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+/[a-z][a-z0-9-]*$
bool IsSourceName(std::string_view s) {
  const size_t slash = s.find('/');
  return slash != std::string_view::npos && IsPluginId(s.substr(0, slash)) &&
         IsLowerId(s.substr(slash + 1));
}

// ^(xdg:)?[A-Za-z0-9_.-]+$
bool IsIconName(std::string_view s) {
  if (s.starts_with("xdg:")) {
    s.remove_prefix(4);
  }
  return !s.empty() && std::ranges::all_of(s, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '_' || c == '.' || c == '-';
  });
}

// ^(\.)?(/([^/~]|~[01])*)*$
bool IsBindPointer(std::string_view s) {
  if (s.starts_with(".")) {
    s.remove_prefix(1);
  }
  if (!s.empty() && s.front() != '/') {
    return false;
  }
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '~' &&
        (i + 1 >= s.size() || (s[i + 1] != '0' && s[i + 1] != '1'))) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------- checking

std::string EscapeToken(std::string_view token) {
  std::string out;
  for (char c : token) {
    if (c == '~') {
      out += "~0";
    } else if (c == '/') {
      out += "~1";
    } else {
      out += c;
    }
  }
  return out;
}

std::string Join(std::string_view path, std::string_view token) {
  return base::StrCat({path, "/", EscapeToken(token)});
}

std::string Join(std::string_view path, size_t index) {
  return base::StrCat({path, "/", base::NumberToString(index)});
}

template <typename Range>
bool Contains(const Range& range, std::string_view value) {
  return std::ranges::find(range, value) != std::ranges::end(range);
}

bool IsNumber(const base::Value& v) {
  return v.is_int() || v.is_double();
}

bool IsInteger(const base::Value& v) {
  if (v.is_int()) {
    return true;
  }
  return v.is_double() && std::isfinite(v.GetDouble()) &&
         v.GetDouble() == std::trunc(v.GetDouble());
}

class Checker {
 public:
  std::optional<ParseError> error;

  bool Fail(std::string path, std::string message) {
    if (!error) {
      error = ParseError{std::move(path), std::move(message)};
    }
    return false;
  }

  bool CheckBinding(const base::Value& v, const std::string& path) {
    const base::DictValue* dict = v.GetIfDict();
    if (!dict) {
      return Fail(path, "expected a binding object");
    }
    for (const auto [key, value] : *dict) {
      if (key == "$bind") {
        if (!value.is_string() || !IsBindPointer(value.GetString())) {
          return Fail(Join(path, key),
                      "$bind must be a JSON Pointer, optionally starting "
                      "with . inside a repeat");
        }
      } else if (key == "format") {
        static constexpr auto kFormats = std::to_array<std::string_view>(
            {"text", "percent", "duration", "bytes", "time", "relative-time"});
        if (!value.is_string() || !Contains(kFormats, value.GetString())) {
          return Fail(Join(path, key), "unknown format");
        }
      } else if (key == "source") {
        if (!value.is_string() || !IsSourceName(value.GetString())) {
          return Fail(Join(path, key),
                      "source must be <plugin id>/<source name>");
        }
      } else {
        return Fail(Join(path, key),
                    base::StrCat({"unknown binding property \"", key, "\""}));
      }
    }
    if (!dict->contains("$bind")) {
      return Fail(path, "a binding needs $bind");
    }
    return true;
  }

  bool CheckOption(const base::Value& v, const std::string& path) {
    const base::DictValue* dict = v.GetIfDict();
    if (!dict) {
      return Fail(path, "an option must be an object");
    }
    for (const auto [key, value] : *dict) {
      if (key != "value" && key != "label" && key != "icon") {
        return Fail(Join(path, key),
                    base::StrCat({"unknown option property \"", key, "\""}));
      }
      if (!value.is_string()) {
        return Fail(Join(path, key), "an option's fields are strings");
      }
    }
    if (!dict->contains("value") || !dict->contains("label")) {
      return Fail(path, "an option needs value and label");
    }
    return true;
  }

  bool CheckAction(const base::Value& v, const std::string& path) {
    const base::DictValue* dict = v.GetIfDict();
    if (!dict) {
      return Fail(path, "an action must be an object");
    }
    for (const auto [key, value] : *dict) {
      const std::string at = Join(path, key);
      if (key == "command") {
        if (IsBinding(value)) {
          if (!CheckBinding(value, at)) {
            return false;
          }
        } else if (!value.is_string() || !IsCommandName(value.GetString())) {
          return Fail(at,
                      "command must be a command name, <plugin id>/<command> "
                      "or a binding");
        }
      } else if (key == "args") {
        if (!value.is_dict()) {
          return Fail(at, "args must be an object");
        }
      } else if (key == "confirm") {
        if (!CheckValue(Kind::kText, value, at, PropSpec{})) {
          return false;
        }
      } else if (key == "close") {
        if (!value.is_bool()) {
          return Fail(at, "close must be a boolean");
        }
      } else {
        return Fail(at,
                    base::StrCat({"unknown action property \"", key, "\""}));
      }
    }
    if (!dict->contains("command")) {
      return Fail(path, "an action needs command");
    }
    return true;
  }

  // Literal-or-binding: a binding must be well formed, a literal must pass.
  bool LiteralOrBinding(const base::Value& v,
                        const std::string& path,
                        bool literal_ok,
                        std::string_view expected) {
    if (IsBinding(v)) {
      return CheckBinding(v, path);
    }
    if (!literal_ok) {
      return Fail(path, base::StrCat({"expected ", expected}));
    }
    return true;
  }

  bool CheckValue(Kind kind,
                  const base::Value& v,
                  const std::string& path,
                  const PropSpec& spec) {
    switch (kind) {
      case Kind::kText:
        return LiteralOrBinding(v, path, v.is_string(),
                                "a string or a binding");
      case Kind::kBool:
        return LiteralOrBinding(v, path, v.is_bool(), "a boolean or a binding");
      case Kind::kNumber:
        return LiteralOrBinding(v, path, IsNumber(v), "a number or a binding");
      case Kind::kIcon:
        return LiteralOrBinding(v, path,
                                v.is_string() && IsIconName(v.GetString()),
                                "an icon name or a binding");
      case Kind::kEnum:
        if (!v.is_string() || !Contains(spec.values, v.GetString())) {
          return Fail(path,
                      base::StrCat({"expected one of ",
                                    base::JoinString(spec.values, ", ")}));
        }
        return true;
      case Kind::kInteger:
        if (!IsInteger(v) || v.GetDouble() < spec.min ||
            v.GetDouble() > spec.max) {
          return Fail(path,
                      base::StrCat({"expected an integer from ",
                                    base::NumberToString(spec.min), " to ",
                                    base::NumberToString(spec.max)}));
        }
        return true;
      case Kind::kAction:
        return CheckAction(v, path);
      case Kind::kBinding:
        return CheckBinding(v, path);
      case Kind::kId:
        if (!v.is_string() || !IsLowerId(v.GetString())) {
          return Fail(path, "expected a lower-case id");
        }
        return true;
      case Kind::kString:
        if (!v.is_string()) {
          return Fail(path, "expected a string");
        }
        return true;
      case Kind::kBoolLiteral:
        if (!v.is_bool()) {
          return Fail(path, "expected a boolean");
        }
        return true;
      case Kind::kOptions:
        if (IsBinding(v)) {
          return CheckBinding(v, path);
        }
        if (!v.is_list()) {
          return Fail(path, "expected an array of options or a binding");
        }
        for (size_t i = 0; i < v.GetList().size(); ++i) {
          if (!CheckOption(v.GetList()[i], Join(path, i))) {
            return false;
          }
        }
        return true;
      case Kind::kOptionList:
        if (!v.is_list() || v.GetList().size() < 2 || v.GetList().size() > 5) {
          return Fail(path, "expected an array of 2 to 5 options");
        }
        for (size_t i = 0; i < v.GetList().size(); ++i) {
          if (!CheckOption(v.GetList()[i], Join(path, i))) {
            return false;
          }
        }
        return true;
      case Kind::kNode:
      case Kind::kChildren:
        // Parsed by ParseNode.
        return true;
    }
    NOTREACHED();
  }

  std::unique_ptr<UiNode> ParseNode(const base::Value& v, std::string path) {
    const base::DictValue* dict = v.GetIfDict();
    if (!dict) {
      Fail(path, "a node must be an object");
      return nullptr;
    }
    const std::string* type = dict->FindString("type");
    if (!type) {
      Fail(Join(path, "type"), "a node needs a string type");
      return nullptr;
    }
    const NodeSpec* spec = FindSpec(*type);
    if (!spec) {
      Fail(Join(path, "type"),
           base::StrCat({"unknown node type \"", *type, "\""}));
      return nullptr;
    }
    auto node = std::make_unique<UiNode>();
    node->type = spec->type;
    node->path = path;

    for (const auto [key, value] : *dict) {
      if (key == "type") {
        continue;
      }
      const std::string at = Join(path, key);
      const auto it = std::ranges::find(spec->props, key, &PropSpec::name);
      if (it == spec->props.end()) {
        Fail(at,
             base::StrCat({"unknown property \"", key, "\" of ", spec->name}));
        return nullptr;
      }
      if (it->kind == Kind::kNode) {
        std::unique_ptr<UiNode> child = ParseNode(value, at);
        if (!child) {
          return nullptr;
        }
        if (key == "child") {
          node->child = std::move(child);
        } else if (key == "template") {
          node->template_node = std::move(child);
        } else {
          node->trailing = std::move(child);
        }
        continue;
      }
      if (it->kind == Kind::kChildren) {
        if (!value.is_list()) {
          Fail(at, "children must be an array of nodes");
          return nullptr;
        }
        for (size_t i = 0; i < value.GetList().size(); ++i) {
          std::unique_ptr<UiNode> child =
              ParseNode(value.GetList()[i], Join(at, i));
          if (!child) {
            return nullptr;
          }
          node->children.push_back(std::move(child));
        }
        continue;
      }
      if (!CheckValue(it->kind, value, at, *it)) {
        return nullptr;
      }
      node->props.Set(key, value.Clone());
    }
    for (const PropSpec& prop : spec->props) {
      if (prop.required && !dict->contains(prop.name)) {
        Fail(path, base::StrCat({spec->name, " needs \"", prop.name, "\""}));
        return nullptr;
      }
    }
    return node;
  }
};

}  // namespace

UiNode::UiNode() = default;
UiNode::~UiNode() = default;
UiTree::UiTree() = default;
UiTree::~UiTree() = default;

std::string ParseError::ToString() const {
  return path.empty() ? message : base::StrCat({path, ": ", message});
}

std::string_view NodeTypeName(NodeType type) {
  const NodeSpec* spec = FindSpec(type);
  CHECK(spec);
  return spec->name;
}

std::optional<NodeType> NodeTypeFromName(std::string_view name) {
  const NodeSpec* spec = FindSpec(name);
  if (!spec) {
    return std::nullopt;
  }
  return spec->type;
}

bool IsBinding(const base::Value& value) {
  return value.is_dict() && value.GetDict().contains("$bind");
}

base::expected<std::unique_ptr<UiTree>, ParseError> ParseUiTree(
    const base::Value& document) {
  Checker checker;
  const base::DictValue* dict = document.GetIfDict();
  if (!dict) {
    return base::unexpected(ParseError{"", "a ui tree must be an object"});
  }
  for (const auto [key, value] : *dict) {
    if (key == "$schema") {
      if (!value.is_string()) {
        return base::unexpected(ParseError{"/$schema", "expected a string"});
      }
    } else if (key == "schemaVersion") {
      if (!IsInteger(value) || value.GetDouble() != 1) {
        return base::unexpected(
            ParseError{"/schemaVersion", "schemaVersion must be 1"});
      }
    } else if (key != "root") {
      return base::unexpected(ParseError{
          Join("", key), base::StrCat({"unknown property \"", key, "\""})});
    }
  }
  if (!dict->contains("schemaVersion")) {
    return base::unexpected(ParseError{"", "a ui tree needs schemaVersion"});
  }
  const base::Value* root = dict->Find("root");
  if (!root) {
    return base::unexpected(ParseError{"", "a ui tree needs root"});
  }
  auto tree = std::make_unique<UiTree>();
  tree->root = checker.ParseNode(*root, "/root");
  if (!tree->root) {
    CHECK(checker.error);
    return base::unexpected(*checker.error);
  }
  return tree;
}

base::expected<std::unique_ptr<UiTree>, ParseError> ParseUiTreeJson(
    std::string_view json) {
  std::optional<base::Value> document =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!document) {
    return base::unexpected(ParseError{"", "not JSON"});
  }
  return ParseUiTree(*document);
}

}  // namespace views_shell::ui_tree
