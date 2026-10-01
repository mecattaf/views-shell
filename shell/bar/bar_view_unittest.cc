// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/bar/bar_view.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/views/background.h"
#include "views_shell/bar/clock_view.h"
#include "views_shell/style/layout_provider.h"

namespace views_shell {
namespace {

// Views need a LayoutProvider (the kit's) and fonts from the ResourceBundle,
// and the clock runs on mock time.
class BarTestBase : public testing::Test {
 protected:
  void SetUp() override {
    static const bool kPathsRegistered = [] {
      ui::RegisterPathProvider();
      return true;
    }();
    ASSERT_TRUE(kPathsRegistered);
    base::FilePath pak;
    ASSERT_TRUE(base::PathService::Get(ui::UI_TEST_PAK, &pak));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(pak);
    layout_provider_ = std::make_unique<ShellLayoutProvider>();
  }
  void TearDown() override {
    layout_provider_.reset();
    ui::ResourceBundle::CleanupSharedInstance();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<ShellLayoutProvider> layout_provider_;
};

using ClockViewTest = BarTestBase;
using BarViewTest = BarTestBase;

TEST(ClockFormatTest, FormatsLocalHoursAndMinutes) {
  base::Time::Exploded exploded = {.year = 2026,
                                   .month = 10,
                                   .day_of_month = 2,
                                   .hour = 7,
                                   .minute = 5,
                                   .second = 59};
  base::Time time;
  ASSERT_TRUE(base::Time::FromLocalExploded(exploded, &time));
  EXPECT_EQ(ClockView::FormatTime(time), u"07:05");
  exploded.hour = 23;
  exploded.minute = 59;
  ASSERT_TRUE(base::Time::FromLocalExploded(exploded, &time));
  EXPECT_EQ(ClockView::FormatTime(time), u"23:59");
}

TEST(ClockFormatTest, DelayToNextMinute) {
  const base::Time on_the_minute =
      base::Time::UnixEpoch() + base::Days(20000) + base::Minutes(7);
  EXPECT_EQ(ClockView::DelayToNextMinute(on_the_minute), base::Minutes(1));
  EXPECT_EQ(ClockView::DelayToNextMinute(on_the_minute + base::Seconds(1)),
            base::Seconds(59));
  EXPECT_EQ(
      ClockView::DelayToNextMinute(on_the_minute + base::Milliseconds(59999)),
      base::Milliseconds(1));
}

// The clock ticks exactly on each minute and keeps one wake-up pending.
TEST_F(ClockViewTest, TicksOnTheMinute) {
  const base::Clock* clock = task_environment_.GetMockClock();
  // Start 20 s into a minute.
  task_environment_.FastForwardBy(
      ClockView::DelayToNextMinute(clock->Now()) + base::Seconds(20));
  ClockView view(clock);
  const std::u16string first = view.GetText();
  EXPECT_EQ(first, ClockView::FormatTime(clock->Now()));
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
  EXPECT_EQ(task_environment_.NextMainThreadPendingTaskDelay(),
            base::Seconds(40));

  task_environment_.FastForwardBy(base::Seconds(40) - base::Milliseconds(1));
  EXPECT_EQ(view.GetText(), first);
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_NE(view.GetText(), first);
  EXPECT_EQ(view.GetText(), ClockView::FormatTime(clock->Now()));

  // From here on, one tick per minute, each on the minute.
  for (int minute = 0; minute < 3; ++minute) {
    EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
    EXPECT_EQ(task_environment_.NextMainThreadPendingTaskDelay(),
              base::Minutes(1));
    const std::u16string before = view.GetText();
    task_environment_.FastForwardBy(base::Minutes(1));
    EXPECT_NE(view.GetText(), before);
    EXPECT_EQ(view.GetText(), ClockView::FormatTime(clock->Now()));
  }
}

TEST_F(BarViewTest, ClockSitsAtTheRightEdge) {
  BarView bar(task_environment_.GetMockClock());
  bar.SetBounds(0, 0, 1920, 32);
  bar.DeprecatedLayoutImmediately();

  EXPECT_TRUE(bar.left_section()->children().empty());
  const gfx::Rect clock = bar.clock_view()->bounds();
  const int padding = layout_provider_->GetDistanceMetric(
      views::DISTANCE_RELATED_CONTROL_HORIZONTAL);
  EXPECT_EQ(clock.right(), 1920 - padding);
  EXPECT_EQ(clock.height(), 32);
  EXPECT_GT(clock.width(), 0);
  EXPECT_EQ(bar.left_section()->bounds().x(), padding);
  EXPECT_EQ(bar.left_section()->bounds().right(), clock.x());
}

TEST_F(BarViewTest, ColoursAreThemeIds) {
  BarView bar(task_environment_.GetMockClock());
  ASSERT_TRUE(bar.background());
  EXPECT_EQ(bar.background()->color(), ui::kColorSysBase);
  EXPECT_EQ(BarView::kBackgroundColorId, ui::kColorSysBase);
  EXPECT_EQ(BarView::kTextColorId, ui::kColorSysOnSurface);
}

}  // namespace
}  // namespace views_shell
