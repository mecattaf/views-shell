// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The bar's workspace strip: one stock views::LabelButton per workspace of
// the WmModel snapshot, in the compositor's order, labelled with the
// workspace's name (its id when the name is empty). It sits in the bar's left
// section (bar/bar_view.h).
//
// Rule R6 (command, then observed echo): the strip draws only what the model
// reports. A click calls WmModel::FocusWorkspace and changes nothing; the
// focused button moves when the compositor's echo arrives as a snapshot and
// the model calls OnSnapshotApplied. The strip is rebuilt from that callback
// (and once at construction, from the snapshot the model already holds), and
// from no other path.
//
// Every colour is a ui::ColorProvider role, so the strip wears the theme
// (rule R17, style/theme-map.json): a focused workspace is kColorSysOnPrimary
// ink on a kColorSysPrimary pill, an urgent one kColorSysOnError on
// kColorSysError (urgency wins over focus), the others plain
// kColorSysOnSurfaceSecondary ink on the bar's ground.
//
// The class lives in views_shell::bar because tabs/workspace_strip.h (the
// ported Chrome tab strip of --left-tabs) already owns views_shell::WorkspaceStrip.

#ifndef VIEWS_SHELL_BAR_WORKSPACE_STRIP_H_
#define VIEWS_SHELL_BAR_WORKSPACE_STRIP_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/color/color_id.h"
#include "ui/views/view.h"
#include "views_shell/wm/wm_model.h"

namespace views {
class LabelButton;
}

namespace views_shell::bar {

class WorkspaceStrip : public views::View, public WmModel::Observer {
  METADATA_HEADER(WorkspaceStrip, views::View)

 public:
  // The roles a button is drawn with.
  static constexpr ui::ColorId kFocusedBackgroundId = ui::kColorSysPrimary;
  static constexpr ui::ColorId kFocusedTextId = ui::kColorSysOnPrimary;
  static constexpr ui::ColorId kUrgentBackgroundId = ui::kColorSysError;
  static constexpr ui::ColorId kUrgentTextId = ui::kColorSysOnError;
  static constexpr ui::ColorId kTextId = ui::kColorSysOnSurfaceSecondary;

  // What one button shows; for tests and the --demo-workspace-switch log.
  struct ButtonState {
    std::string workspace;  // WmWorkspace::id
    std::u16string label;
    bool focused = false;
    bool urgent = false;
  };

  // `model` must outlive the strip. The strip shows the model's current
  // snapshot at once (empty before the first one) and follows it.
  explicit WorkspaceStrip(WmModel* model);
  WorkspaceStrip(const WorkspaceStrip&) = delete;
  WorkspaceStrip& operator=(const WorkspaceStrip&) = delete;
  ~WorkspaceStrip() override;

  // The buttons, in order.
  std::vector<ButtonState> GetButtonStates() const;
  // The button of workspace `id`, or nullptr.
  views::LabelButton* GetButtonForWorkspace(const std::string& id);
  // How many times the strip rebuilt itself from a model callback.
  int rebuild_count() const { return rebuild_count_; }

  // WmModel::Observer:
  void OnSnapshotApplied() override;
  void OnDisconnected() override;

 private:
  struct Entry {
    std::string workspace;
    raw_ptr<views::LabelButton> button = nullptr;
    bool focused = false;
    bool urgent = false;
  };

  // Brings the buttons in line with the model's snapshot: buttons are reused
  // by workspace id, created, removed and reordered as the snapshot says.
  void Rebuild();
  // Applies `entry`'s focused/urgent state to its button's colours.
  static void Style(const Entry& entry);
  // A button was pressed: ask the compositor, change nothing (rule R6).
  void OnButtonPressed(const std::string& workspace);

  const raw_ptr<WmModel> model_;
  std::vector<Entry> entries_;
  int rebuild_count_ = 0;
  base::ScopedObservation<WmModel, WmModel::Observer> observation_{this};
};

}  // namespace views_shell::bar

#endif  // VIEWS_SHELL_BAR_WORKSPACE_STRIP_H_
