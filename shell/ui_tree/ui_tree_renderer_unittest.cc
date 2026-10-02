// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The renderer with stock Views:
//
//   RenderTraceTest     every examples/*/ui/*.json against its snapshot
//                       (tools/fixtures/snapshots) builds views whose trace is
//                       byte for byte tools/fixtures/render/<dir>.<file>.json,
//                       and, with a display, the root sits in a real Widget
//   UiTreeRendererTest  actions, Rebind, render errors, the icon table
//   ListItemViewTest    the composed row
//
// views::ViewsTestBase needs aura::Env, which on this Wayland-only build
// connects to a compositor (WAYLAND_DISPLAY): with a display (the bench runs
// these inside runtime-test against a private headless scroll,
// tools/bench/seq/w3a.sh) every rendered root is shown in a Widget; without
// one, the same views are built and checked outside a Widget, and the test
// says so. The umbrella's main sets up no UI resources, so the fixture loads
// the ui test pak (debt D2) and, with a display, does what a Views test suite
// does once per process (as the notifications tests do).

#include "views_shell/ui_tree/ui_tree_renderer.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/task_environment.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "mojo/core/embedder/embedder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gl/test/gl_surface_test_support.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/slider.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/test_views_delegate.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "views_shell/style/layout_provider.h"
#include "views_shell/ui_tree/list_item_view.h"
#include "views_shell/ui_tree/render_trace.h"

namespace views_shell::ui_tree {
namespace {

// The 14 ui files of examples/ (RenderTraceTest.CoversEveryExample checks
// this list against the directory).
constexpr const char* kExampleUiFiles[] = {
    "music-scratchpad/button.json",
    "power-menu/button.json",
    "power-menu/panel.json",
    "quick-settings-audio/input.json",
    "quick-settings-audio/output.json",
    "quick-settings-audio/page.json",
    "quick-settings-bluetooth/page.json",
    "quick-settings-bluetooth/tile.json",
    "quick-settings-brightness/slider.json",
    "quick-settings-dnd/tile.json",
    "quick-settings-media/card.json",
    "quick-settings-network/page.json",
    "quick-settings-network/tile.json",
    "quick-settings-power-footer/footer.json",
};

base::FilePath DataRoot() {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data");
}

std::string ReadText(const base::FilePath& path) {
  std::string text;
  CHECK(base::ReadFileToString(path, &text)) << path;
  return text;
}

base::Value ReadJson(const base::FilePath& path) {
  std::optional<base::Value> v =
      base::JSONReader::Read(ReadText(path), base::JSON_PARSE_RFC);
  CHECK(v) << path;
  return std::move(*v);
}

bool HaveWaylandDisplay() {
  auto env = base::Environment::Create();
  return env->HasVar("WAYLAND_DISPLAY") || env->HasVar("WAYLAND_SOCKET");
}

// What views::ViewsTestSuite does before any Views test, once per process;
// only needed (and only possible) with a display.
void ViewsProcessSetup() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  mojo::core::Init();
  gl::GLSurfaceTestSupport::InitializeOneOff();
  static base::NoDestructor<base::TestDiscardableMemoryAllocator> allocator;
  base::DiscardableMemoryAllocator::SetInstance(allocator.get());
}

// views::ViewsTestBase, held rather than inherited: its destructor CHECKs that
// SetUp ran, and a fixture without a display must not run it.
class ViewsHarness : public views::ViewsTestBase {
 public:
  void SetUp() override { views::ViewsTestBase::SetUp(); }
  void TearDown() override { views::ViewsTestBase::TearDown(); }
  views::TestViewsDelegate* delegate() { return test_views_delegate(); }

 private:
  void TestBody() override {}
};

class RecordingDelegate : public UiTreeDelegate {
 public:
  void OnAction(const UiAction& action) override {
    UiAction copy;
    copy.command = action.command;
    copy.args = action.args.Clone();
    copy.confirm = action.confirm;
    copy.close = action.close;
    copy.path = action.path;
    actions.push_back(std::move(copy));
  }
  std::vector<UiAction> actions;
};

class UiTreeViewsTest : public testing::Test {
 protected:
  void SetUp() override {
    static const bool kPaths = [] {
      ui::RegisterPathProvider();
      return true;
    }();
    ASSERT_TRUE(kPaths);
    if (!ui::ResourceBundle::HasSharedInstance()) {
      base::FilePath pak;
      ASSERT_TRUE(base::PathService::Get(ui::UI_TEST_PAK, &pak));
      ui::ResourceBundle::InitSharedInstanceWithPakPath(pak);
      owns_resource_bundle_ = true;
    }
    if (HaveWaylandDisplay()) {
      ViewsProcessSetup();
      ax_platform_.emplace();
      harness_ = std::make_unique<ViewsHarness>();
      harness_->SetUp();
      // The kit's metrics and typography, as the program installs them.
      harness_->delegate()->set_layout_provider(
          std::make_unique<ShellLayoutProvider>());
    } else {
      task_environment_.emplace();
      ax_platform_.emplace();
      layout_provider_ = std::make_unique<ShellLayoutProvider>();
    }
  }

  void TearDown() override {
    widgets_.clear();
    rendered_.clear();
    if (harness_) {
      harness_->TearDown();
      harness_.reset();
    }
    layout_provider_.reset();
    ax_platform_.reset();
    task_environment_.reset();
    if (owns_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
    }
  }

  bool has_display() const { return harness_ != nullptr; }

  // Renders and, with a display, shows the root in a Widget. Returns the
  // tree; it stays alive until TearDown (the views with it).
  RenderedTree* Render(std::string_view json,
                       std::optional<Snapshot> snapshot,
                       const RenderOptions& options = RenderOptions()) {
    auto tree = ParseUiTreeJson(json);
    CHECK(tree.has_value()) << tree.error().ToString();
    rendered_.push_back(RenderUiTree(std::move(tree).value(),
                                     std::move(snapshot), options, &delegate_));
    RenderedTree* rendered = rendered_.back().get();
    Host(rendered);
    return rendered;
  }

  void Host(RenderedTree* rendered) {
    if (!has_display() || !rendered->root_view()) {
      return;
    }
    std::unique_ptr<views::Widget> widget = harness_->CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    views::View* root = rendered->root_view();
    widget->SetContentsView(rendered->TakeRootView());
    widget->Show();
    widget->LayoutRootViewIfNecessary();
    EXPECT_EQ(root->GetWidget(), widget.get());
    EXPECT_TRUE(root->GetVisible());
    widgets_.push_back(std::move(widget));
  }

  static Snapshot SnapshotOf(std::string_view json) {
    std::optional<base::Value> v =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    CHECK(v);
    return Snapshot::FromJson(*v).value();
  }

  RecordingDelegate delegate_;
  std::vector<std::unique_ptr<RenderedTree>> rendered_;
  std::vector<std::unique_ptr<views::Widget>> widgets_;

 private:
  bool owns_resource_bundle_ = false;
  std::optional<base::test::TaskEnvironment> task_environment_;
  std::optional<ui::AXPlatformForTest> ax_platform_;
  std::unique_ptr<ViewsHarness> harness_;
  std::unique_ptr<ShellLayoutProvider> layout_provider_;
};

// ---------------------------------------------------------------- goldens

class RenderTraceTest : public UiTreeViewsTest,
                        public testing::WithParamInterface<const char*> {};

// Every error a trace may carry is one of the documented forms
// (schemas/ui-tree-rendering.md, "Render errors").
bool IsDocumentedError(const std::string& e) {
  return e == "not drawn in chapter 2" ||
         base::EndsWith(e, " not drawn in chapter 2") ||
         base::StartsWith(e, "unknown icon: ") ||
         base::StartsWith(e, "image ") ||
         base::StartsWith(e, "items does not resolve to an array: ");
}

void CheckErrors(const RenderNode& node) {
  for (const std::string& e : node.errors) {
    EXPECT_TRUE(IsDocumentedError(e)) << node.path << ": " << e;
  }
  for (const auto& c : node.children) {
    CheckErrors(*c);
  }
}

TEST_P(RenderTraceTest, MatchesGolden) {
  const std::string file = GetParam();
  const std::vector<std::string> parts =
      base::SplitString(file, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(parts.size(), 2u);
  const std::string& dir = parts[0];
  const base::FilePath plugin_dir =
      DataRoot().AppendASCII("examples").AppendASCII(dir);

  RenderOptions options;
  const base::Value manifest =
      ReadJson(plugin_dir.AppendASCII("views-shell-plugin.json"));
  options.plugin_id = *manifest.GetDict().FindString("id");
  options.plugin_dir = plugin_dir;
  options.clock = FormatClock::ForTrace();
  Snapshot snapshot =
      Snapshot::FromJson(ReadJson(DataRoot()
                                      .AppendASCII("fixtures/snapshots")
                                      .AppendASCII(dir + ".json")))
          .value();

  RenderedTree* rendered =
      Render(ReadText(plugin_dir.AppendASCII("ui").AppendASCII(parts[1])),
             std::move(snapshot), options);

  const std::string trace = SerializeTrace(RenderTrace(*rendered));
  const base::FilePath golden_path = DataRoot()
                                         .AppendASCII("fixtures/render")
                                         .AppendASCII(dir + "." + parts[1]);
  const std::string golden = ReadText(golden_path);
  // Canonically equal (parsed), and byte for byte (the reference's format).
  EXPECT_EQ(base::JSONReader::Read(trace, base::JSON_PARSE_RFC),
            base::JSONReader::Read(golden, base::JSON_PARSE_RFC));
  EXPECT_EQ(trace, golden) << golden_path;

  CheckErrors(rendered->root());
  if (rendered->root().view) {
    // A drawn root: the trace's view class is the built view's.
    EXPECT_TRUE(rendered->root().drawn);
  } else {
    // An undrawn root reports why, and nothing blank stands in.
    EXPECT_FALSE(rendered->errors().empty());
    EXPECT_EQ(rendered->root_view(), nullptr);
  }
  if (!has_display()) {
    GTEST_LOG_(INFO) << file << ": built without a Widget (no WAYLAND_DISPLAY)";
  }
}

INSTANTIATE_TEST_SUITE_P(,
                         RenderTraceTest,
                         testing::ValuesIn(kExampleUiFiles),
                         [](const testing::TestParamInfo<const char*>& info) {
                           std::string name = info.param;
                           for (char& c : name) {
                             if (!base::IsAsciiAlphaNumeric(c)) {
                               c = '_';
                             }
                           }
                           return name;
                         });

TEST(RenderTraceListTest, CoversEveryExample) {
  std::vector<std::string> found;
  base::FileEnumerator plugins(DataRoot().AppendASCII("examples"), false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath plugin = plugins.Next(); !plugin.empty();
       plugin = plugins.Next()) {
    base::FileEnumerator files(plugin.AppendASCII("ui"), false,
                               base::FileEnumerator::FILES, "*.json");
    for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
      found.push_back(plugin.BaseName().MaybeAsASCII() + "/" +
                      f.BaseName().MaybeAsASCII());
    }
  }
  std::ranges::sort(found);
  std::vector<std::string> listed(std::begin(kExampleUiFiles),
                                  std::end(kExampleUiFiles));
  EXPECT_EQ(found, listed);
}

// ---------------------------------------------------------------- behaviour

using UiTreeRendererTest = UiTreeViewsTest;

constexpr char kActions[] = R"({"schemaVersion":1,"root":{"type":"column",
  "children":[
    {"type":"button","label":{"$bind":"/name"},
     "action":{"command":"open","args":{"what":{"$bind":"/name"}},
               "confirm":"Sure?","close":true}},
    {"type":"switch","value":{"$bind":"/on"},
     "action":{"command":"other.plugin/set-on"}},
    {"type":"checkbox","label":"Check","value":false,
     "action":{"command":"check"}},
    {"type":"radioGroup","options":{"$bind":"/opts"},"value":"b",
     "action":{"command":"pick"}},
    {"type":"select","options":{"$bind":"/opts"},"value":"a",
     "accessibleName":"Choose","action":{"command":"choose"}},
    {"type":"repeat","items":{"$bind":"/rows"},"key":"./id",
     "template":{"type":"listItem","label":{"$bind":"./label"},
       "action":{"command":{"$bind":"./id"},"args":{"row":{"$bind":"./id"}}}}}
  ]}})";

constexpr char kActionsSnapshot[] = R"({"snapshot":{"name":"Docs","on":true,
  "opts":[{"value":"a","label":"A"},{"value":"b","label":"B"}],
  "rows":[{"id":"first","label":"First"},{"id":"second","label":"Second"}]}})";

void Press(views::Button* button) {
  ui::MouseEvent e(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                   base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                   ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(button).NotifyClick(e);
}

// A button press, with no platform needed: the action reaches the delegate
// resolved and qualified.
TEST_F(UiTreeRendererTest, ButtonPressFiresResolvedAction) {
  RenderOptions options;
  options.plugin_id = "example.power-menu";
  RenderedTree* rendered = Render(
      R"({"schemaVersion":1,"root":{"type":"column","children":[
        {"type":"button","label":{"$bind":"/label"},"variant":"primary",
         "action":{"command":"lock","args":{"who":{"$bind":"/user"},
                   "n":2},"confirm":{"$bind":"/ask"}}}]}})",
      SnapshotOf(
          R"({"snapshot":{"label":"Lock","user":"tom","ask":"Lock now?"}})"),
      options);
  const RenderNode& button = *rendered->root().children[0];
  EXPECT_EQ(button.view_class, "views::MdTextButton");
  Press(static_cast<views::Button*>(button.view.get()));
  ASSERT_EQ(delegate_.actions.size(), 1u);
  EXPECT_EQ(delegate_.actions[0].command, "example.power-menu/lock");
  EXPECT_EQ(delegate_.actions[0].args,
            base::DictValue().Set("n", 2).Set("who", "tom"));
  EXPECT_EQ(delegate_.actions[0].confirm, "Lock now?");
  EXPECT_FALSE(delegate_.actions[0].close);
}

// Every input kind. A ToggleButton's or Checkbox's click ripple reads the
// widget's colour provider, so this needs a Widget (a display).
TEST_F(UiTreeRendererTest, InputsFireResolvedActions) {
  if (!has_display()) {
    GTEST_SKIP() << "stock toggle and checkbox clicks need a Widget (no "
                    "WAYLAND_DISPLAY)";
  }
  RenderOptions options;
  options.plugin_id = "example.test";
  RenderedTree* rendered =
      Render(kActions, SnapshotOf(kActionsSnapshot), options);
  const RenderNode& root = rendered->root();
  ASSERT_EQ(root.children.size(), 6u);
  EXPECT_TRUE(rendered->errors().empty());

  Press(static_cast<views::Button*>(root.children[0]->view.get()));
  ASSERT_EQ(delegate_.actions.size(), 1u);
  EXPECT_EQ(delegate_.actions[0].command, "example.test/open");
  EXPECT_EQ(delegate_.actions[0].args, base::DictValue().Set("what", "Docs"));
  EXPECT_EQ(delegate_.actions[0].confirm, "Sure?");
  EXPECT_TRUE(delegate_.actions[0].close);
  EXPECT_EQ(delegate_.actions[0].path, "/root/children/0");

  // switch: on was true, a press turns it off; a qualified command stays.
  Press(static_cast<views::Button*>(root.children[1]->view.get()));
  ASSERT_EQ(delegate_.actions.size(), 2u);
  EXPECT_EQ(delegate_.actions[1].command, "other.plugin/set-on");
  EXPECT_EQ(delegate_.actions[1].args, base::DictValue().Set("value", false));

  Press(static_cast<views::Button*>(root.children[2]->view.get()));
  EXPECT_EQ(delegate_.actions.back().args,
            base::DictValue().Set("value", true));

  // radioGroup: one RadioButton per option, the bound value checked.
  const RenderNode& radio = *root.children[3];
  ASSERT_EQ(radio.children.size(), 2u);
  auto* a = static_cast<views::RadioButton*>(radio.children[0]->view.get());
  auto* b = static_cast<views::RadioButton*>(radio.children[1]->view.get());
  EXPECT_FALSE(a->GetChecked());
  EXPECT_TRUE(b->GetChecked());
  Press(a);
  EXPECT_EQ(delegate_.actions.back().command, "example.test/pick");
  EXPECT_EQ(delegate_.actions.back().args, base::DictValue().Set("value", "a"));

  auto* combobox = static_cast<views::Combobox*>(root.children[4]->view.get());
  EXPECT_EQ(combobox->GetSelectedIndex(), 0u);
  combobox->MenuSelectionAt(1);
  EXPECT_EQ(delegate_.actions.back().command, "example.test/choose");
  EXPECT_EQ(delegate_.actions.back().args, base::DictValue().Set("value", "b"));

  // The repeat's copies sit in the column; a bound command is qualified.
  const RenderNode& repeat = *root.children[5];
  EXPECT_EQ(repeat.view, nullptr);
  ASSERT_EQ(repeat.children.size(), 2u);
  EXPECT_EQ(repeat.children[1]->view->parent(), root.view);
  Press(static_cast<views::Button*>(repeat.children[1]->view.get()));
  EXPECT_EQ(delegate_.actions.back().command, "example.test/second");
  EXPECT_EQ(delegate_.actions.back().args,
            base::DictValue().Set("row", "second"));
  EXPECT_EQ(delegate_.actions.size(), 6u);
}

// views::Textfield asks Ozone for the clipboard at construction, so it needs
// the platform a display brings up.
TEST_F(UiTreeRendererTest, TextfieldFiresOnEnter) {
  if (!has_display()) {
    GTEST_SKIP() << "views::Textfield needs the Ozone platform (no "
                    "WAYLAND_DISPLAY)";
  }
  RenderOptions options;
  options.plugin_id = "example.test";
  RenderedTree* rendered = Render(
      R"({"schemaVersion":1,"root":{"type":"textfield","value":"typed",
          "placeholder":"Search","action":{"command":"submit"}}})",
      Snapshot(), options);
  auto* textfield = static_cast<views::Textfield*>(rendered->root().view.get());
  EXPECT_EQ(textfield->GetText(), u"typed");
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  static_cast<views::View*>(textfield)->OnKeyPressed(enter);
  EXPECT_EQ(delegate_.actions.back().command, "example.test/submit");
  EXPECT_EQ(delegate_.actions.back().args,
            base::DictValue().Set("value", "typed"));
}

TEST_F(UiTreeRendererTest, SliderFiresOnceAtTheEndOfADrag) {
  RenderedTree* rendered = Render(
      R"({"schemaVersion":1,"root":{"type":"slider","value":{"$bind":"/v"},
          "accessibleName":"Volume","action":{"command":"set"}}})",
      SnapshotOf(R"({"snapshot":{"v":0.25}})"));
  auto* slider = static_cast<views::Slider*>(rendered->root().view.get());
  EXPECT_FLOAT_EQ(slider->GetValue(), 0.25f);
  // The value the renderer sets fires nothing.
  EXPECT_TRUE(delegate_.actions.empty());
  if (!slider->GetWidget()) {
    slider->SetBounds(0, 0, 200, slider->GetPreferredSize().height());
  }
  views::View* view = slider;
  const gfx::Point at(slider->width() * 3 / 4, slider->height() / 2);
  ui::MouseEvent press(ui::EventType::kMousePressed, at, at,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, at, at,
                         base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  // Pressing moves the thumb (a user change) but fires nothing mid-drag.
  ASSERT_TRUE(view->OnMousePressed(press));
  EXPECT_TRUE(delegate_.actions.empty());
  view->OnMouseReleased(release);
  ASSERT_EQ(delegate_.actions.size(), 1u);
  EXPECT_EQ(delegate_.actions[0].command, "set");
  const std::optional<double> value =
      delegate_.actions[0].args.FindDouble("value");
  ASSERT_TRUE(value);
  EXPECT_GT(*value, 0.5);
  EXPECT_FLOAT_EQ(*value, slider->GetValue());
  // A keyboard step fires at once.
  ui::KeyEvent right(ui::EventType::kKeyPressed, ui::VKEY_RIGHT, ui::EF_NONE);
  ASSERT_TRUE(view->OnKeyPressed(right));
  EXPECT_EQ(delegate_.actions.size(), 2u);
}

TEST_F(UiTreeRendererTest, RebindUpdatesValuesInPlace) {
  constexpr char kTree[] = R"({"schemaVersion":1,"root":{"type":"column",
    "children":[
      {"type":"switch","value":{"$bind":"/powered"},
       "action":{"command":"set-powered"}},
      {"type":"label","text":{"$bind":"/battery","format":"percent"},
       "visible":{"$bind":"/hasBattery"}},
      {"type":"repeat","items":{"$bind":"/devices"},"key":"./id",
       "template":{"type":"listItem","label":{"$bind":"./name"},
         "selected":{"$bind":"./connected"}}}]}})";
  RenderedTree* rendered = Render(kTree, SnapshotOf(R"({"snapshot":{
      "powered":true,"battery":0.855,"hasBattery":true,
      "devices":[{"id":"a","name":"Headphones","connected":true}]}})"));
  const RenderNode& root = rendered->root();
  auto* toggle =
      static_cast<views::ToggleButton*>(root.children[0]->view.get());
  auto* label = static_cast<views::Label*>(root.children[1]->view.get());
  auto* item =
      static_cast<ListItemView*>(root.children[2]->children[0]->view.get());
  EXPECT_TRUE(toggle->GetIsOn());
  EXPECT_EQ(label->GetText(), u"86%");
  EXPECT_EQ(item->label()->GetText(), u"Headphones");
  EXPECT_TRUE(item->selected());

  EXPECT_EQ(rendered->Rebind(SnapshotOf(R"({"snapshot":{
      "powered":false,"battery":0.5,"hasBattery":false,
      "devices":[{"id":"a","name":"Earbuds","connected":false}]}})")),
            RenderedTree::RebindResult::kInPlace);
  // The same views, new values.
  EXPECT_EQ(root.children[0]->view, toggle);
  EXPECT_EQ(root.children[2]->children[0]->view, item);
  EXPECT_FALSE(toggle->GetIsOn());
  EXPECT_EQ(label->GetText(), u"50%");
  EXPECT_FALSE(label->GetVisible());
  EXPECT_EQ(item->label()->GetText(), u"Earbuds");
  EXPECT_FALSE(item->selected());
  EXPECT_EQ(*root.children[1]->props.FindString("text"), "50%");

  // A second device changes the structure: nothing is touched.
  EXPECT_EQ(rendered->Rebind(SnapshotOf(R"({"snapshot":{
      "powered":true,"battery":0.5,"hasBattery":false,
      "devices":[{"id":"a","name":"X","connected":false},
                 {"id":"b","name":"Y","connected":false}]}})")),
            RenderedTree::RebindResult::kStructureChanged);
  EXPECT_FALSE(toggle->GetIsOn());
  EXPECT_EQ(item->label()->GetText(), u"Earbuds");
}

TEST_F(UiTreeRendererTest, UndrawnNodesAreErrorsNeverBlankViews) {
  RenderedTree* rendered = Render(
      R"({"schemaVersion":1,"root":{"type":"column","children":[
        {"type":"markdown","text":"*hi*"},
        {"type":"tile","icon":"settings","label":"T"},
        {"type":"tabSlider","value":"a","action":{"command":"x"},
         "options":[{"value":"a","label":"A"},{"value":"b","label":"B"}]},
        {"type":"keyChips","keys":"Mod+L"},
        {"type":"mediaSession","session":{"$bind":"/s"}},
        {"type":"icon","icon":"xdg:audio-volume-high"},
        {"type":"iconButton","icon":"power","accessibleName":"P",
         "action":{"command":"x"}},
        {"type":"progress","shape":"ring"},
        {"type":"label","text":"drawn"}]}})",
      SnapshotOf(R"({"snapshot":{"s":{}}})"));
  const RenderNode& root = rendered->root();
  ASSERT_EQ(root.children.size(), 9u);
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(root.children[i]->view, nullptr) << root.children[i]->node;
    EXPECT_FALSE(root.children[i]->errors.empty());
  }
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(root.children[i]->errors[0], "not drawn in chapter 2");
  }
  EXPECT_EQ(root.children[5]->errors[0], "unknown icon: xdg:audio-volume-high");
  EXPECT_EQ(root.children[6]->errors[0], "unknown icon: power");
  EXPECT_EQ(root.children[7]->errors[0], "ring not drawn in chapter 2");
  // Only the label became a child view of the column.
  EXPECT_EQ(root.view->children().size(), 1u);
  const std::vector<RenderError> errors = rendered->errors();
  ASSERT_EQ(errors.size(), 8u);
  EXPECT_EQ(errors[0].path, "/root/children/0");
}

TEST_F(UiTreeRendererTest, UndrawnRootGivesNoView) {
  RenderedTree* rendered =
      Render(R"({"schemaVersion":1,"root":{"type":"keyChips","keys":"a"}})",
             Snapshot());
  EXPECT_EQ(rendered->root_view(), nullptr);
  EXPECT_EQ(rendered->TakeRootView(), nullptr);
  ASSERT_EQ(rendered->errors().size(), 1u);
  EXPECT_EQ(rendered->errors()[0].message, "not drawn in chapter 2");
}

// The compiled icon table is the document's table, row for row.
TEST_F(UiTreeRendererTest, IconTableMatchesTheDocument) {
  const std::string doc =
      ReadText(DataRoot().AppendASCII("schemas/ui-tree-rendering.md"));
  const size_t start = doc.find("\n## Icon names\n");
  ASSERT_NE(start, std::string::npos);
  size_t end = doc.find("\n## ", start + 1);
  const std::string section = doc.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
  std::vector<std::pair<std::string, std::string>> rows;
  for (const std::string& line : base::SplitString(
           section, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    // | `name` | `ns::kSymbolIcon` |
    if (!base::StartsWith(line, "| `")) {
      continue;
    }
    const std::vector<std::string> cells = base::SplitString(
        line, "|", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    ASSERT_EQ(cells.size(), 2u) << line;
    std::string name, symbol;
    base::TrimString(cells[0], "`", &name);
    base::TrimString(cells[1], "`", &symbol);
    rows.emplace_back(name, symbol);
  }
  const std::vector<IconRow> table = IconTable();
  ASSERT_EQ(rows.size(), table.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    EXPECT_EQ(rows[i].first, table[i].name);
    EXPECT_EQ(rows[i].second, table[i].symbol);
    EXPECT_TRUE(FindIcon(rows[i].first));
  }
  EXPECT_FALSE(FindIcon("power"));
  EXPECT_FALSE(FindIcon("xdg:lock"));
}

// ---------------------------------------------------------------- ListItemView

using ListItemViewTest = UiTreeViewsTest;

TEST_F(ListItemViewTest, ComposesIconLabelsAndTrailing) {
  int presses = 0;
  auto item = std::make_unique<ListItemView>(
      base::BindRepeating([](int* n) { ++*n; }, &presses));
  EXPECT_FALSE(item->icon_view()->GetVisible());
  EXPECT_FALSE(item->sublabel()->GetVisible());
  item->SetIcon(ui::ImageModel::FromVectorIcon(*FindIcon("lock")));
  item->SetLabel(u"Lock");
  item->SetSublabel(u"Mod+Ctrl+L");
  auto* trailing = item->SetTrailingView(std::make_unique<views::Label>(u"!"));
  EXPECT_TRUE(item->icon_view()->GetVisible());
  EXPECT_TRUE(item->sublabel()->GetVisible());
  EXPECT_EQ(item->trailing_view(), trailing);
  EXPECT_EQ(trailing->parent(), item.get());
  EXPECT_EQ(item->GetClassName(), "ListItemView");
  item->SetSelected(true);
  EXPECT_TRUE(item->selected());
  EXPECT_NE(item->background(), nullptr);
  item->SetSelected(false);
  EXPECT_EQ(item->background(), nullptr);
  Press(item.get());
  EXPECT_EQ(presses, 1);
  item->SetTrailingView(nullptr);
  EXPECT_EQ(item->trailing_view(), nullptr);
}

}  // namespace
}  // namespace views_shell::ui_tree
