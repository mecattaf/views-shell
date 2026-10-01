// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab/glow_hover_controller.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_GLOW_HOVER_CONTROLLER_H_
#define VIEWS_SHELL_TABS_GLOW_HOVER_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "views_shell/tabs/tab_style.h"

namespace base {
class TimeDelta;
}

namespace views {
class View;
}

namespace views_shell {

// GlowHoverController is responsible for drawing a hover effect and is used by
// the TabStrip. Typical usage:
//   OnMouseEntered() -> invoke Show().
//   OnMouseMoved()   -> invoke SetLocation().
//   OnMouseExited()  -> invoke Hide().
//   OnPaint()        -> if ShouldDraw() returns true invoke Draw().
// Internally GlowHoverController uses an animation to animate the glow and
// invokes SchedulePaint() back on the View as necessary.
class GlowHoverController : public views::AnimationDelegateViews {
 public:
  GlowHoverController(views::View* view,
                      base::TimeDelta duration = base::Milliseconds(200));
  GlowHoverController(const GlowHoverController&) = delete;
  GlowHoverController& operator=(const GlowHoverController&) = delete;
  ~GlowHoverController() override;

  // Sets the AnimationContainer used by the animation.
  void SetAnimationContainer(gfx::AnimationContainer* container);

  // Set opacity scale to use when Show is called with SUBTLE.
  void SetSubtleOpacityScale(double opacity_scale);

  // Initiates showing the hover.
  void Show(TabStyle::ShowHoverStyle style);

  // Hides the hover.
  void Hide(TabStyle::HideHoverStyle style);

  // Returns the value of the animation.
  double GetAnimationValue() const;

  SkAlpha GetAlpha() const;

  // Returns true if there is something to be drawn. Use this instead of
  // invoking Draw() if creating `mask_image` is expensive.
  bool ShouldDraw() const;

  // views::AnimationDelegateViews overrides:
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationProgressed(const gfx::Animation* animation) override;

  gfx::SlideAnimation* animation_for_testing() { return &animation_; }

 private:
  // View we're drawing to.
  raw_ptr<views::View> view_;

  // Opacity of the glow ramps up over time.
  gfx::SlideAnimation animation_;

  // The opacity animation duration.
  base::TimeDelta animation_duration_;

  double opacity_scale_;
  double subtle_opacity_scale_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_GLOW_HOVER_CONTROLLER_H_
