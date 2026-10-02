// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The render trace of a drawn ui tree (schemas/ui-tree-rendering.md, "The
// render trace"): for every node, the stock class of the view that was built
// (read from the view itself), its layout, its resolved props, its children and
// its render error. SerializeTrace() writes it as tools/ui-tree-render.py does
// (sorted keys, two-space indent, non-ASCII as is, one final newline), so the
// C++ trace of an example is byte for byte its tools/fixtures/render golden.

#ifndef VIEWS_SHELL_UI_TREE_RENDER_TRACE_H_
#define VIEWS_SHELL_UI_TREE_RENDER_TRACE_H_

#include <string>

#include "base/values.h"

namespace views_shell::ui_tree {

class RenderedTree;
struct RenderNode;

// {"plugin": <id or null>, "root": <node>}.
base::Value RenderTrace(const RenderedTree& tree);

// One node: {node, view, layout, props, children, error}.
base::Value TraceNode(const RenderNode& node);

// The trace as text, exactly as the reference tool prints it.
std::string SerializeTrace(const base::Value& trace);

}  // namespace views_shell::ui_tree

#endif  // VIEWS_SHELL_UI_TREE_RENDER_TRACE_H_
