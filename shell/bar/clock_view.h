// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The bar's clock: the local time as HH:MM in a views::Label, in the theme's
// colours (the bar sets them through ui::ColorProvider ids, never literals).
//
// It wakes once a minute, exactly on the minute: a base::OneShotTimer is armed
// for the time left until the next wall-clock minute and re-armed on every
// tick, so an idle bar has one pending wake-up per minute and nothing else
// (0 % CPU at idle), and the label never drifts from the wall clock the way a
// fixed 60 s repeating timer would. Rule R18 forbids timers as systemd units,
// not a UI timer inside the process.

#ifndef VIEWS_SHELL_BAR_CLOCK_VIEW_H_
#define VIEWS_SHELL_BAR_CLOCK_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"

namespace views_shell {

class ClockView : public views::Label {
  METADATA_HEADER(ClockView, views::Label)

 public:
  // `clock` is the wall clock (tests pass a mock); it must outlive the view.
  explicit ClockView(const base::Clock* clock);
  ClockView(const ClockView&) = delete;
  ClockView& operator=(const ClockView&) = delete;
  ~ClockView() override;

  // "HH:MM" in local time, 24-hour.
  static std::u16string FormatTime(base::Time time);

  // The time from `now` to the start of the next minute: in (0, 60 s].
  static base::TimeDelta DelayToNextMinute(base::Time now);

  // When the next tick is due (for tests and diagnostics).
  base::TimeTicks next_tick_for_testing() const {
    return timer_.desired_run_time();
  }

 private:
  // Shows the current time and arms the timer for the next minute.
  void Tick();

  const raw_ptr<const base::Clock> clock_;
  base::OneShotTimer timer_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_BAR_CLOCK_VIEW_H_
