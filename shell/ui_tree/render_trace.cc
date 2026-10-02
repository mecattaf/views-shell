// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/ui_tree/render_trace.h"

#include <memory>
#include <utility>

#include "base/strings/string_util.h"
#include "views_shell/ui_tree/binding.h"
#include "views_shell/ui_tree/ui_tree_renderer.h"

namespace views_shell::ui_tree {
namespace {

base::Value StringOrNull(const std::string& s) {
  return s.empty() ? base::Value() : base::Value(s);
}

}  // namespace

base::Value TraceNode(const RenderNode& node) {
  base::ListValue children;
  for (const std::unique_ptr<RenderNode>& child : node.children) {
    children.Append(TraceNode(*child));
  }
  // The view is read from what was built: a node with no view traces null.
  return base::Value(
      base::DictValue()
          .Set("node", node.node)
          .Set("view", node.view ? base::Value(node.view_class) : base::Value())
          .Set("layout", node.view ? StringOrNull(node.layout) : base::Value())
          .Set("props", node.props.Clone())
          .Set("children", std::move(children))
          .Set("error", node.errors.empty() ? base::Value()
                                            : base::Value(base::JoinString(
                                                  node.errors, "; "))));
}

base::Value RenderTrace(const RenderedTree& tree) {
  return base::Value(base::DictValue()
                         .Set("plugin", StringOrNull(tree.plugin_id()))
                         .Set("root", TraceNode(tree.root())));
}

std::string SerializeTrace(const base::Value& trace) {
  return ToPythonJson(trace, /*pretty=*/true) + "\n";
}

}  // namespace views_shell::ui_tree
