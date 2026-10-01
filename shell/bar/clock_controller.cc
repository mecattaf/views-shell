// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/clock_controller.cc

#include "agency/shell/clock_controller.h"

#include <utility>

#include "agency/producers/clock/clock_producer.h"
#include "base/i18n/time_formatting.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "ui/views/controls/label.h"

namespace agency {

ClockController::ClockController(ClockProducer* producer, views::Label* label)
    : label_(label) {
  // In-process bind: ClockProducer::AddObserver is a public C++ method taking a
  // PendingRemote<ClockObserver>. The producer immediately primes us with its
  // retained snapshot via the first OnClockChanged, then pushes on every
  // timedate1 PropertiesChanged landing.
  producer->AddObserver(receiver_.BindNewPipeAndPassRemote());

  // 1-second ticking display: trivially cheap, no minute-alignment logic, no
  // drift. HH:MM (seconds omitted).
  tick_.Start(FROM_HERE, base::Seconds(1), this, &ClockController::RenderNow);

  RenderNow();  // initial paint before the first snapshot lands
}

ClockController::~ClockController() = default;

void ClockController::OnClockChanged(mojom::ClockSnapshotPtr snapshot) {
  // The producer supplies timezone metadata; base::Time renders wall time.
  // snapshot->time_usec is a point-in-time anchor, intentionally NOT used for
  // the ticking display (see topbar-design.md section 3.3).
  timezone_ = snapshot->timezone;
  VLOG(1) << "[agency] clock snapshot: tz=" << timezone_
          << " ntp_enabled=" << snapshot->ntp_enabled
          << " ntp_synchronized=" << snapshot->ntp_synchronized;
  RenderNow();
}

void ClockController::RenderNow() {
  // MVP renders in the process's local zone (the OS keeps this equal to
  // timedate1's Timezone in the normal case); timezone_ is carried for the
  // ICU-accurate v1.1 follow-up. TimeFormatTimeOfDay returns a std::u16string
  // HH:MM in the current locale's clock convention.
  label_->SetText(base::TimeFormatTimeOfDay(base::Time::Now()));
}

}  // namespace agency
