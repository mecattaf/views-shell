// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/bar/clock_view.h"

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace views_shell {

ClockView::ClockView(const base::Clock* clock) : clock_(clock) {
  Tick();
}

ClockView::~ClockView() = default;

// static
std::u16string ClockView::FormatTime(base::Time time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return base::ASCIIToUTF16(
      base::StringPrintf("%02d:%02d", exploded.hour, exploded.minute));
}

// static
base::TimeDelta ClockView::DelayToNextMinute(base::Time now) {
  // Local minutes start on UTC minutes: every zone offset is whole minutes.
  constexpr int64_t kMinute = base::Minutes(1).InMicroseconds();
  int64_t into_minute =
      (now - base::Time::UnixEpoch()).InMicroseconds() % kMinute;
  if (into_minute < 0) {
    into_minute += kMinute;
  }
  return base::Microseconds(kMinute - into_minute);
}

void ClockView::Tick() {
  const base::Time now = clock_->Now();
  SetText(FormatTime(now));
  timer_.Start(FROM_HERE, DelayToNextMinute(now),
               base::BindOnce(&ClockView::Tick, base::Unretained(this)));
}

BEGIN_METADATA(ClockView)
END_METADATA

}  // namespace views_shell
