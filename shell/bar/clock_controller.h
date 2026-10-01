// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/clock_controller.h
//
// agency::ClockController -- the ClockProducer -> BarView bridge.
//
// Binds one mojom::ClockObserver receiver into ClockProducer (an in-process
// C++ call to the producer's public AddObserver; no Remote<ClockProducer> is
// needed since producer and consumer live in the same browser process) and
// carries the producer's timezone metadata. A 1-second RepeatingTimer renders
// the wall clock: the producer supplies the IANA timezone, base::Time renders
// the ticking display (deriving a ticking clock from the snapshot's
// point-in-time time_usec would drift). See topbar-design.md section 3.3.

#ifndef AGENCY_SHELL_CLOCK_CONTROLLER_H_
#define AGENCY_SHELL_CLOCK_CONTROLLER_H_

#include <string>

#include "agency/mojom/Clock.mojom.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace views {
class Label;
}  // namespace views

namespace agency {

class ClockProducer;

// Owns the observer receiver and the tick timer; drives a single Label with
// formatted HH:MM wall time. Lifetime is bounded by ShellHost (constructed
// after the BarView, destroyed before the ClockProducer).
class ClockController : public mojom::ClockObserver {
 public:
  // `producer` and `label` must outlive this controller (ShellHost teardown
  // order guarantees it: controller -> widget -> producer, so this controller
  // -- and its raw_ptr to `label` -- dies before the widget frees the Label).
  ClockController(ClockProducer* producer, views::Label* label);
  ClockController(const ClockController&) = delete;
  ClockController& operator=(const ClockController&) = delete;
  ~ClockController() override;

  // mojom::ClockObserver:
  void OnClockChanged(mojom::ClockSnapshotPtr snapshot) override;

 private:
  // Formats base::Time::Now() as HH:MM (process local zone) and pushes it to
  // the label. Called on every timer tick and on every snapshot landing.
  void RenderNow();

  raw_ptr<views::Label> label_;
  std::string timezone_;  // last-known IANA zone from the retained snapshot
  mojo::Receiver<mojom::ClockObserver> receiver_{this};
  base::RepeatingTimer tick_;
};

}  // namespace agency

#endif  // AGENCY_SHELL_CLOCK_CONTROLLER_H_
