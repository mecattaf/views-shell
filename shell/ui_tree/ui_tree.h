// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A plugin's ui tree (schemas/ui-tree.schema.json), parsed and checked: every
// node of the schema becomes a typed UiNode, and anything the schema does not
// allow (an unknown node type, an unknown or missing property, a value of the
// wrong kind, an enum value outside its list, a malformed binding or command)
// is a parse error with the JSON path where it was found. Nothing is skipped
// silently: a tree either parses whole or not at all.
//
// The checks are this schema written out in C++, so the renderer does not
// depend on a JSON Schema engine (components/json_schema does not exist at
// 154). views_shell_unittests runs them over every examples/*/ui/*.json, which
// must parse, and every tools/fixtures/invalid/*.ui.json, which must not.

#ifndef VIEWS_SHELL_UI_TREE_UI_TREE_H_
#define VIEWS_SHELL_UI_TREE_UI_TREE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "base/values.h"

namespace views_shell::ui_tree {

// Every node type of the schema.
enum class NodeType {
  kColumn,
  kRow,
  kStack,
  kGrid,
  kScroll,
  kRepeat,
  kLabel,
  kMarkdown,
  kIcon,
  kImage,
  kBadge,
  kDot,
  kSeparator,
  kSpacer,
  kProgress,
  kButton,
  kIconButton,
  kSwitch,
  kCheckbox,
  kRadioGroup,
  kSelect,
  kSlider,
  kTextfield,
  kListItem,
  kTile,
  kTabSlider,
  kKeyChips,
  kMediaSession,
};

// The schema's name for `type` ("iconButton"), and back.
std::string_view NodeTypeName(NodeType type);
std::optional<NodeType> NodeTypeFromName(std::string_view name);

// One node. `props` holds the node's own properties exactly as written, minus
// `type` and the node-valued keys, which become the typed child slots below.
struct UiNode {
  UiNode();
  UiNode(const UiNode&) = delete;
  UiNode& operator=(const UiNode&) = delete;
  ~UiNode();

  NodeType type = NodeType::kColumn;
  // The JSON path of this node in its file ("/root/children/2").
  std::string path;
  base::DictValue props;

  // column, row, stack, grid: `children`.
  std::vector<std::unique_ptr<UiNode>> children;
  // scroll: `child`.
  std::unique_ptr<UiNode> child;
  // repeat: `template`.
  std::unique_ptr<UiNode> template_node;
  // listItem: `trailing`.
  std::unique_ptr<UiNode> trailing;
};

struct UiTree {
  UiTree();
  UiTree(const UiTree&) = delete;
  UiTree& operator=(const UiTree&) = delete;
  ~UiTree();

  int schema_version = 1;
  std::unique_ptr<UiNode> root;
};

struct ParseError {
  // A JSON path into the file ("/root/children/0/action/command"), "" for the
  // document itself.
  std::string path;
  std::string message;

  // "<path>: <message>", or the message alone at the document.
  std::string ToString() const;
};

// Checks and converts a parsed JSON document.
base::expected<std::unique_ptr<UiTree>, ParseError> ParseUiTree(
    const base::Value& document);

// Reads JSON text (RFC 8259, base::JSONReader) and parses it.
base::expected<std::unique_ptr<UiTree>, ParseError> ParseUiTreeJson(
    std::string_view json);

// True when `value` is a binding object ({"$bind": ...}). A parsed tree's
// bindings are already checked; this only tells them from literals.
bool IsBinding(const base::Value& value);

}  // namespace views_shell::ui_tree

#endif  // VIEWS_SHELL_UI_TREE_UI_TREE_H_
