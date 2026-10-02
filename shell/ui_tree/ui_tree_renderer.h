// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Layer 7 of docs/architecture.md: a plugin's ui tree, drawn with stock Views.
//
//   std::unique_ptr<UiTree> tree = ParseUiTreeJson(json).value();
//   std::unique_ptr<RenderedTree> rendered = RenderUiTree(
//       std::move(tree), std::move(snapshot), options, &delegate);
//   surface->AddChildView(rendered->TakeRootView());   // null if not drawn
//   ...
//   rendered->Rebind(std::move(next_snapshot));        // values in place
//
// Each node becomes the stock class of schemas/ui-tree-rendering.md's view
// table (column and row a BoxLayoutView, label a Label, button an MdTextButton,
// listItem a views_shell::ListItemView, ...). Sizes come from the
// LayoutProvider and TypographyProvider in force (the style kit's) and colours
// from ui::ColorIds, never from literals (rules R11, R17). A node or part that
// chapter 2 cannot draw (the five undrawn node types, an icon name outside the
// icon table, ...) is a render error, listed by errors() with its JSON path and
// carried on the node; it is never drawn blank or replaced.
//
// Interactions call UiTreeDelegate::OnAction with the action resolved against
// the latest snapshot. The render trace (render_trace.h) of a RenderedTree is
// the JSON tools/ui-tree-render.py prints for the same tree and snapshot.

#ifndef VIEWS_SHELL_UI_TREE_UI_TREE_RENDERER_H_
#define VIEWS_SHELL_UI_TREE_UI_TREE_RENDERER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "views_shell/ui_tree/binding.h"
#include "views_shell/ui_tree/ui_tree.h"

namespace gfx {
struct VectorIcon;
}

namespace views_shell::ui_tree {

// What an interaction asks the plugin to do: the trace's action, with `args`
// resolved now and, for an input, its new value under "value".
struct UiAction {
  UiAction();
  UiAction(UiAction&&);
  UiAction& operator=(UiAction&&);
  ~UiAction();

  // Qualified ("<plugin id>/<command>") unless the plugin id is empty.
  std::string command;
  base::DictValue args;
  // Ask before invoking (a ui::DialogModel confirmation): the host's job.
  std::optional<std::string> confirm;
  // Close the surface after invoking: the host's job.
  bool close = false;
  // The JSON path of the node that fired.
  std::string path;
};

class UiTreeDelegate {
 public:
  virtual void OnAction(const UiAction& action) = 0;

 protected:
  virtual ~UiTreeDelegate() = default;
};

struct RenderOptions {
  RenderOptions();
  RenderOptions(const RenderOptions&);
  RenderOptions& operator=(const RenderOptions&);
  ~RenderOptions();

  // Qualifies bare command names; empty leaves them bare (trace plugin null).
  std::string plugin_id;
  // `image` nodes read PNG files below this directory; empty means none.
  base::FilePath plugin_dir;
  // time and relative-time: the real clock and the local zone by default.
  FormatClock clock;
};

// One node as drawn: the trace's six fields plus the view built for it.
struct RenderNode {
  RenderNode();
  RenderNode(const RenderNode&) = delete;
  RenderNode& operator=(const RenderNode&) = delete;
  ~RenderNode();

  // The schema's node type, or "option" for a radioGroup's radio buttons.
  std::string node;
  // The view built for the node; null when nothing is drawn for it (a render
  // error, or a repeat whose copies went into its parent).
  raw_ptr<views::View> view = nullptr;
  // "views::" + the view's metadata class name, or views_shell::ListItemView;
  // empty when there is no view.
  std::string view_class;
  // box-vertical, box-horizontal, fill, table, or empty.
  std::string layout;
  base::DictValue props;
  std::vector<std::unique_ptr<RenderNode>> children;
  // Render errors, in check order; the trace joins them with "; ".
  std::vector<std::string> errors;
  // Whether the node gets a view (false: a render error removed it, or an
  // undrawn node type).
  bool drawn = true;
  std::string path;

  // Set while building: the view until its parent takes it.
  std::unique_ptr<views::View> owned_view;
};

struct RenderError {
  std::string path;
  std::string message;
};

class RenderedTree : public views::ViewObserver {
 public:
  enum class RebindResult {
    // Every bound value was updated on the existing views.
    kInPlace,
    // The new snapshot changes the structure (a repeat's items or keys, a
    // radio group's options, which nodes are drawn): nothing was changed;
    // render the tree again.
    kStructureChanged,
    // The root view was destroyed; nothing to update.
    kNoViews,
  };

  RenderedTree(const RenderedTree&) = delete;
  RenderedTree& operator=(const RenderedTree&) = delete;
  ~RenderedTree() override;

  // The root view, owned here until TakeRootView(); null when the root node is
  // not drawn (see errors()).
  views::View* root_view() { return root_view_; }
  std::unique_ptr<views::View> TakeRootView();

  const RenderNode& root() const { return *root_; }
  const std::string& plugin_id() const { return options_.plugin_id; }
  std::vector<RenderError> errors() const;

  // Resolves every binding against `snapshot` and updates the views in place.
  // The views must still exist (kNoViews once the root view is gone).
  RebindResult Rebind(Snapshot snapshot);

  // Fires `key` ("action" or "iconAction") of `node`, with `value` added to
  // the args under "value" when set. Used by the views' callbacks.
  void FireAction(const RenderNode* node,
                  std::string_view key,
                  std::optional<base::Value> value);

  base::WeakPtr<RenderedTree> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // views::ViewObserver:
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  friend std::unique_ptr<RenderedTree> RenderUiTree(std::unique_ptr<UiTree>,
                                                    std::optional<Snapshot>,
                                                    const RenderOptions&,
                                                    UiTreeDelegate*);

  RenderedTree(std::unique_ptr<UiTree> tree,
               std::optional<Snapshot> snapshot,
               const RenderOptions& options,
               UiTreeDelegate* delegate);

  std::unique_ptr<UiTree> tree_;
  std::optional<Snapshot> snapshot_;
  RenderOptions options_;
  raw_ptr<UiTreeDelegate> delegate_;
  std::unique_ptr<RenderNode> root_;
  std::unique_ptr<views::View> owned_root_view_;
  raw_ptr<views::View> root_view_ = nullptr;
  bool views_alive_ = true;
  base::ScopedObservation<views::View, views::ViewObserver> root_observation_{
      this};
  base::WeakPtrFactory<RenderedTree> weak_factory_{this};
};

// Draws `tree` against `snapshot` (nullopt: bindings stay as written, as the
// reference tool does without --snapshot). `delegate` receives the actions and
// must outlive the result.
std::unique_ptr<RenderedTree> RenderUiTree(std::unique_ptr<UiTree> tree,
                                           std::optional<Snapshot> snapshot,
                                           const RenderOptions& options,
                                           UiTreeDelegate* delegate);

// The icon table (schemas/ui-tree-rendering.md, "Icon names"): the stock
// vector icon for a views-shell icon name, or null. No fallback: a name that
// is not in the table is a render error.
const gfx::VectorIcon* FindIcon(std::string_view name);

// Every row of the icon table, name and stock symbol
// ("vector_icons::kLockIcon"), in table order, so a test can hold the document
// and the code to one list.
struct IconRow {
  std::string_view name;
  std::string_view symbol;
};
std::vector<IconRow> IconTable();

}  // namespace views_shell::ui_tree

#endif  // VIEWS_SHELL_UI_TREE_UI_TREE_RENDERER_H_
