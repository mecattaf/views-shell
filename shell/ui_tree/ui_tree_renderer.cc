// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/ui_tree/ui_tree_renderer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ref.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/class_property.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/badge.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/dot_indicator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/slider.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/layout/table_layout.h"
#include "ui/views/layout/table_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/style/typography_provider.h"
#include "ui/views/vector_icons.h"
#include "views_shell/ui_tree/list_item_view.h"

namespace views_shell::ui_tree {

// Interaction glue owned by the view it serves (a ui::ClassProperty), so it
// lives exactly as long as that view and never dangles if the RenderedTree
// goes first: it reaches the tree only through a WeakPtr.
class ViewHook {
 public:
  virtual ~ViewHook() = default;
};

}  // namespace views_shell::ui_tree

DEFINE_UI_CLASS_PROPERTY_TYPE(views_shell::ui_tree::ViewHook*)

namespace views_shell::ui_tree {
namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(ViewHook, kViewHookKey)

constexpr char kNotDrawn[] = "not drawn in chapter 2";

std::string PartNotDrawn(std::string_view part) {
  return base::StrCat({part, " not drawn in chapter 2"});
}

// ---------------------------------------------------------------- icons

struct IconEntry {
  std::string_view name;
  std::string_view symbol;
  raw_ptr<const gfx::VectorIcon> icon;
};

#define VIEWS_SHELL_ICON(name, ns, symbol) {name, #ns "::" #symbol, &ns::symbol}

// schemas/ui-tree-rendering.md "Icon names", row for row (the unit test holds
// the two to one list).
const std::vector<IconEntry>& IconEntries() {
  static const base::NoDestructor<std::vector<IconEntry>> kEntries({
      VIEWS_SHELL_ICON("bluetooth", vector_icons, kBluetoothIcon),
      VIEWS_SHELL_ICON("bluetooth-connected", vector_icons,
                       kBluetoothConnectedIcon),
      VIEWS_SHELL_ICON("bluetooth-disabled", vector_icons,
                       kBluetoothDisabledIcon),
      VIEWS_SHELL_ICON("bluetooth-searching", vector_icons,
                       kBluetoothSearchingIcon),
      VIEWS_SHELL_ICON("cast", vector_icons, kCastIcon),
      VIEWS_SHELL_ICON("check", views, kCheckIcon),
      VIEWS_SHELL_ICON("close", vector_icons, kCloseIcon),
      VIEWS_SHELL_ICON("desktop", vector_icons, kDesktopWindowsIcon),
      VIEWS_SHELL_ICON("devices", vector_icons, kDevicesIcon),
      VIEWS_SHELL_ICON("do-not-disturb", vector_icons, kNotificationsOffIcon),
      VIEWS_SHELL_ICON("error", vector_icons, kErrorIcon),
      VIEWS_SHELL_ICON("globe", vector_icons, kGlobeIcon),
      VIEWS_SHELL_ICON("headset", vector_icons, kHeadphonesIcon),
      VIEWS_SHELL_ICON("help", vector_icons, kHelpIcon),
      VIEWS_SHELL_ICON("home", vector_icons, kHomeIcon),
      VIEWS_SHELL_ICON("info", vector_icons, kInfoIcon),
      VIEWS_SHELL_ICON("keyboard", vector_icons, kKeyboardIcon),
      VIEWS_SHELL_ICON("lock", vector_icons, kLockIcon),
      VIEWS_SHELL_ICON("mic", vector_icons, kMicIcon),
      VIEWS_SHELL_ICON("mic-off", vector_icons, kMicOffIcon),
      VIEWS_SHELL_ICON("more", views, kMoreHorizIcon),
      VIEWS_SHELL_ICON("mouse", vector_icons, kTouchpadMouseIcon),
      VIEWS_SHELL_ICON("network-wired", vector_icons, kSettingsEthernetIcon),
      VIEWS_SHELL_ICON("notifications", vector_icons, kNotificationsIcon),
      VIEWS_SHELL_ICON("open-in-new", views, kOpenInNewIcon),
      VIEWS_SHELL_ICON("pause", vector_icons, kPauseIcon),
      VIEWS_SHELL_ICON("play", vector_icons, kPlayArrowIcon),
      VIEWS_SHELL_ICON("refresh", vector_icons, kRefreshIcon),
      VIEWS_SHELL_ICON("restart", vector_icons, kReplayIcon),
      VIEWS_SHELL_ICON("search", vector_icons, kSearchIcon),
      VIEWS_SHELL_ICON("settings", vector_icons, kSettingsIcon),
      VIEWS_SHELL_ICON("skip-next", vector_icons, kSkipNextIcon),
      VIEWS_SHELL_ICON("skip-previous", vector_icons, kSkipPreviousIcon),
      VIEWS_SHELL_ICON("speaker", vector_icons, kVolumeUpIcon),
      VIEWS_SHELL_ICON("tune", vector_icons, kTuneIcon),
      VIEWS_SHELL_ICON("usb", vector_icons, kUsbIcon),
      VIEWS_SHELL_ICON("volume-medium", vector_icons, kVolumeUpIcon),
      VIEWS_SHELL_ICON("volume-muted", vector_icons, kVolumeOffIcon),
      VIEWS_SHELL_ICON("warning", vector_icons, kWarningIcon),
  });
  return *kEntries;
}

#undef VIEWS_SHELL_ICON

// ---------------------------------------------------------------- values

std::optional<std::u16string> TextOf(const base::Value* v) {
  if (!v || v->is_dict() || v->is_list()) {
    return std::nullopt;
  }
  return base::UTF8ToUTF16(AsText(*v));
}

std::optional<bool> BoolOf(const base::Value* v) {
  if (!v || !v->is_bool()) {
    return std::nullopt;
  }
  return v->GetBool();
}

std::optional<double> NumberOf(const base::Value* v) {
  if (!v || !(v->is_int() || v->is_double())) {
    return std::nullopt;
  }
  return v->GetDouble();
}

std::string StringOr(const base::DictValue& props,
                     std::string_view key,
                     std::string_view fallback) {
  const std::string* s = props.FindString(key);
  return s ? *s : std::string(fallback);
}

// ---------------------------------------------------------------- the kit

int SpacingOf(std::string_view token, bool horizontal) {
  const views::LayoutProvider* p = views::LayoutProvider::Get();
  if (token == "tight") {
    return p->GetDistanceMetric(views::DISTANCE_RELATED_LABEL_HORIZONTAL);
  }
  if (token == "normal") {
    return p->GetDistanceMetric(horizontal
                                    ? views::DISTANCE_RELATED_CONTROL_HORIZONTAL
                                    : views::DISTANCE_RELATED_CONTROL_VERTICAL);
  }
  if (token == "loose") {
    return p->GetDistanceMetric(
        horizontal ? views::DISTANCE_UNRELATED_CONTROL_HORIZONTAL
                   : views::DISTANCE_UNRELATED_CONTROL_VERTICAL);
  }
  return 0;
}

int LineHeight(int style) {
  return views::TypographyProvider::Get().GetLineHeight(
      views::style::CONTEXT_LABEL, style);
}

int IconSizeOf(std::string_view token) {
  if (token == "xsmall") {
    return LineHeight(views::style::STYLE_CAPTION);
  }
  if (token == "small") {
    return LineHeight(views::style::STYLE_BODY_5);
  }
  if (token == "large") {
    return LineHeight(views::style::STYLE_HEADLINE_4);
  }
  if (token == "xlarge") {
    return LineHeight(views::style::STYLE_HEADLINE_1);
  }
  return LineHeight(views::style::STYLE_BODY_3);
}

int TextStyleOf(std::string_view token) {
  if (token == "display") {
    return views::style::STYLE_HEADLINE_1;
  }
  if (token == "title") {
    return views::style::STYLE_HEADLINE_4;
  }
  if (token == "headline") {
    return views::style::STYLE_HEADLINE_5;
  }
  if (token == "body-strong") {
    return views::style::STYLE_BODY_3_MEDIUM;
  }
  if (token == "button") {
    return views::style::STYLE_BODY_4_MEDIUM;
  }
  if (token == "annotation") {
    return views::style::STYLE_BODY_5;
  }
  if (token == "label") {
    return views::style::STYLE_CAPTION_MEDIUM;
  }
  return views::style::STYLE_BODY_3;
}

// Semantic roles to colour ids (schemas/ui-tree-rendering.md). positive and
// warning have no kColorSys role at this tag (style/theme-map.json).
std::optional<ui::ColorId> RoleColor(const std::string* role, bool for_icon) {
  if (!role) {
    return std::nullopt;
  }
  if (*role == "default") {
    return ui::kColorSysOnSurface;
  }
  if (*role == "subtle") {
    return ui::kColorSysOnSurfaceSubtle;
  }
  if (*role == "primary") {
    return ui::kColorSysPrimary;
  }
  if (*role == "positive") {
    return ui::kColorAlertLowSeverity;
  }
  if (*role == "warning") {
    return for_icon ? ui::kColorAlertMediumSeverityIcon
                    : ui::kColorAlertMediumSeverityText;
  }
  if (*role == "alert") {
    return ui::kColorSysError;
  }
  if (*role == "disabled") {
    return ui::kColorSysStateDisabled;
  }
  return std::nullopt;
}

int CornerRadius() {
  return views::LayoutProvider::Get()->GetCornerRadiusMetric(
      views::Emphasis::kMedium);
}

// ---------------------------------------------------------------- images

struct ImageLoad {
  SkBitmap bitmap;
  std::optional<std::string> error;
};

ImageLoad LoadImage(const base::Value* src,
                    const base::FilePath& plugin_dir,
                    bool has_snapshot) {
  ImageLoad out;
  if (!src || (!has_snapshot && IsBinding(*src))) {
    return out;
  }
  if (!src->is_string()) {
    out.error = base::StrCat({"image src is not a path: ", AsText(*src)});
    return out;
  }
  const std::string& s = src->GetString();
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      s, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (s.empty() || s.starts_with("/") ||
      std::ranges::find(parts, "..") != parts.end()) {
    out.error = base::StrCat({"image outside the plugin directory: ", s});
    return out;
  }
  if (plugin_dir.empty()) {
    out.error = base::StrCat({"image has no plugin directory: ", s});
    return out;
  }
  const base::FilePath path = plugin_dir.AppendASCII(s);
  std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(path);
  if (!bytes || base::DirectoryExists(path)) {
    out.error = base::StrCat({"image not found: ", s});
    return out;
  }
  out.bitmap = gfx::PNGCodec::Decode(*bytes);
  if (out.bitmap.isNull()) {
    out.error = base::StrCat({"image is not a PNG: ", s});
  }
  return out;
}

// ---------------------------------------------------------------- hooks

class SliderHook : public ViewHook, public views::SliderListener {
 public:
  SliderHook(base::WeakPtr<RenderedTree> tree, const RenderNode* node)
      : tree_(std::move(tree)), node_(node) {}

  // views::SliderListener: a drag fires once, at its end.
  void SliderValueChanged(views::Slider* sender,
                          float value,
                          float old_value,
                          views::SliderChangeReason reason) override {
    if (reason != views::SliderChangeReason::kByUser || dragging_) {
      return;
    }
    Fire(value);
  }
  void SliderDragStarted(views::Slider* sender) override { dragging_ = true; }
  void SliderDragEnded(views::Slider* sender) override {
    dragging_ = false;
    Fire(sender->GetValue());
  }

 private:
  void Fire(float value) {
    if (tree_) {
      tree_->FireAction(node_, "action",
                        base::Value(static_cast<double>(value)));
    }
  }

  base::WeakPtr<RenderedTree> tree_;
  raw_ptr<const RenderNode> node_;
  bool dragging_ = false;
};

class TextfieldHook : public ViewHook, public views::TextfieldController {
 public:
  TextfieldHook(base::WeakPtr<RenderedTree> tree, const RenderNode* node)
      : tree_(std::move(tree)), node_(node) {}

  // views::TextfieldController: Enter fires the action with the text.
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override {
    if (key_event.type() != ui::EventType::kKeyPressed ||
        key_event.key_code() != ui::VKEY_RETURN) {
      return false;
    }
    if (tree_) {
      tree_->FireAction(node_, "action",
                        base::Value(base::UTF16ToUTF8(sender->GetText())));
    }
    return true;
  }

 private:
  base::WeakPtr<RenderedTree> tree_;
  raw_ptr<const RenderNode> node_;
};

// views::DotIndicator takes SkColors only: this resolves the role's colour id
// through the widget's colour provider on every theme change.
class DotHook : public ViewHook, public views::ViewObserver {
 public:
  explicit DotHook(views::DotIndicator* dot) : dot_(dot) {
    observation_.Observe(dot);
  }

  void SetColorId(std::optional<ui::ColorId> id) {
    id_ = id;
    Apply();
  }

  // views::ViewObserver:
  void OnViewThemeChanged(views::View* view) override { Apply(); }
  void OnViewIsDeleting(views::View* view) override { observation_.Reset(); }

 private:
  void Apply() {
    if (!id_ || !observation_.IsObserving()) {
      return;
    }
    if (const ui::ColorProvider* provider = dot_->GetColorProvider()) {
      dot_->SetColor(provider->GetColor(*id_),
                     provider->GetColor(ui::kColorSysSurface));
    }
  }

  raw_ptr<views::DotIndicator> dot_;
  std::optional<ui::ColorId> id_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

// ---------------------------------------------------------------- the build

std::string ViewClassName(const views::View* view) {
  const std::string_view name = view->GetClassName();
  if (name == "ListItemView") {
    return "views_shell::ListItemView";
  }
  return base::StrCat({"views::", name});
}

// The view table of schemas/ui-tree-rendering.md: class and layout.
std::pair<std::string_view, std::string_view> ViewTableRow(NodeType type) {
  switch (type) {
    case NodeType::kColumn:
      return {"views::BoxLayoutView", "box-vertical"};
    case NodeType::kRow:
      return {"views::BoxLayoutView", "box-horizontal"};
    case NodeType::kStack:
      return {"views::View", "fill"};
    case NodeType::kGrid:
      return {"views::TableLayoutView", "table"};
    case NodeType::kScroll:
      return {"views::ScrollView", ""};
    case NodeType::kLabel:
      return {"views::Label", ""};
    case NodeType::kIcon:
    case NodeType::kImage:
      return {"views::ImageView", ""};
    case NodeType::kBadge:
      return {"views::Badge", ""};
    case NodeType::kDot:
      return {"views::DotIndicator", ""};
    case NodeType::kSeparator:
      return {"views::Separator", ""};
    case NodeType::kSpacer:
      return {"views::View", ""};
    case NodeType::kProgress:
      return {"views::ProgressBar", ""};
    case NodeType::kButton:
      return {"views::MdTextButton", ""};
    case NodeType::kIconButton:
      return {"views::ImageButton", ""};
    case NodeType::kSwitch:
      return {"views::ToggleButton", ""};
    case NodeType::kCheckbox:
      return {"views::Checkbox", ""};
    case NodeType::kRadioGroup:
      return {"views::BoxLayoutView", "box-vertical"};
    case NodeType::kSelect:
      return {"views::Combobox", ""};
    case NodeType::kSlider:
      return {"views::Slider", ""};
    case NodeType::kTextfield:
      return {"views::Textfield", ""};
    case NodeType::kListItem:
      return {"views_shell::ListItemView", ""};
    case NodeType::kRepeat:
    case NodeType::kMarkdown:
    case NodeType::kTile:
    case NodeType::kTabSlider:
    case NodeType::kKeyChips:
    case NodeType::kMediaSession:
      return {"", ""};
  }
  NOTREACHED();
}

bool IsUndrawn(NodeType type) {
  return type == NodeType::kMarkdown || type == NodeType::kTile ||
         type == NodeType::kTabSlider || type == NodeType::kKeyChips ||
         type == NodeType::kMediaSession;
}

class Builder {
 public:
  Builder(RenderedTree* tree,
          BindingResolver& resolver,
          const RenderOptions& options,
          bool make_views)
      : tree_(tree),
        resolver_(resolver),
        options_(options),
        make_views_(make_views) {}

  // `single_slot`: the node fills a one-child slot (the root, scroll.child,
  // listItem.trailing). `in_row`: its parent lays out horizontally.
  std::unique_ptr<RenderNode> Build(const UiNode& n,
                                    bool single_slot,
                                    bool in_row) {
    auto out = std::make_unique<RenderNode>();
    out->node = std::string(NodeTypeName(n.type));
    out->path = n.path;
    for (const auto [key, value] : n.props) {
      if (key == "$schema") {
        continue;
      }
      out->props.Set(key, key == "action" || key == "iconAction"
                              ? ResolveAction(value)
                              : resolver_->ResolveValue(value));
    }
    const auto [view_class, layout] = ViewTableRow(n.type);
    out->view_class = std::string(view_class);
    out->layout = std::string(layout);

    if (n.type == NodeType::kRepeat) {
      BuildRepeat(n, *out, single_slot, in_row);
      return out;
    }

    out->drawn = CheckNode(n.type, *out);
    if (!out->drawn) {
      out->view_class.clear();
      out->layout.clear();
    }

    if (n.type == NodeType::kRadioGroup) {
      BuildOptions(*out);
    }
    const bool row = n.type == NodeType::kRow;
    for (const std::unique_ptr<UiNode>& c : n.children) {
      out->children.push_back(Build(*c, false, row));
    }
    if (n.child) {
      out->children.push_back(Build(*n.child, true, false));
    }
    if (n.trailing) {
      out->children.push_back(Build(*n.trailing, true, true));
    }

    if (make_views_ && out->drawn) {
      CreateView(n.type, *out, in_row);
      ApplyProps(n.type, *out);
      AttachChildren(n.type, *out);
    }
    return out;
  }

  // Applies a node's resolved props to its view; also used by Rebind.
  static void ApplyProps(NodeType type, RenderNode& node);
  static void ApplyOption(RenderNode& option);
  static void ApplyCommon(RenderNode& node);

 private:
  base::Value ResolveAction(const base::Value& raw) {
    const base::DictValue& a = raw.GetDict();
    base::Value command = resolver_->ResolveValue(*a.Find("command"));
    if (command.is_string() &&
        command.GetString().find('/') == std::string::npos &&
        !options_->plugin_id.empty()) {
      command = base::Value(
          base::StrCat({options_->plugin_id, "/", command.GetString()}));
    }
    const base::Value* args = a.Find("args");
    const base::Value* confirm = a.Find("confirm");
    return base::Value(
        base::DictValue()
            .Set("command", std::move(command))
            .Set("args", args ? resolver_->ResolveValue(*args)
                              : base::Value(base::DictValue()))
            .Set("confirm",
                 confirm ? resolver_->ResolveValue(*confirm) : base::Value())
            .Set("close", a.FindBool("close").value_or(false)));
  }

  std::optional<std::string> IconError(const base::Value* v) {
    if (!v || (!resolver_->has_snapshot() && IsBinding(*v))) {
      return std::nullopt;
    }
    if (!v->is_string()) {
      return base::StrCat({"unknown icon: ", AsText(*v)});
    }
    if (!FindIcon(v->GetString())) {
      return base::StrCat({"unknown icon: ", v->GetString()});
    }
    return std::nullopt;
  }

  // The render errors of a node, in tools/ui-tree-render.py's node_errors()
  // order. Returns whether the node gets a view.
  bool CheckNode(NodeType type, RenderNode& node) {
    const base::DictValue& p = node.props;
    std::vector<std::string>& errors = node.errors;
    if (IsUndrawn(type)) {
      errors.push_back(kNotDrawn);
      return false;
    }
    switch (type) {
      case NodeType::kIcon:
      case NodeType::kIconButton: {
        if (std::optional<std::string> e = IconError(p.Find("icon"))) {
          errors.push_back(*e);
          return false;
        }
        const std::string* variant = p.FindString("variant");
        if (type == NodeType::kIconButton && variant &&
            (*variant == "prominent" || *variant == "floating")) {
          errors.push_back(PartNotDrawn("variant"));
        }
        return true;
      }
      case NodeType::kImage: {
        ImageLoad load = LoadImage(p.Find("src"), options_->plugin_dir,
                                   resolver_->has_snapshot());
        if (load.error) {
          errors.push_back(*load.error);
          return false;
        }
        return true;
      }
      case NodeType::kProgress: {
        const std::string* shape = p.FindString("shape");
        if (shape && *shape == "ring") {
          errors.push_back(PartNotDrawn("ring"));
          return false;
        }
        return true;
      }
      case NodeType::kButton:
      case NodeType::kListItem: {
        if (p.contains("icon")) {
          if (std::optional<std::string> e = IconError(p.Find("icon"))) {
            errors.push_back(*e);
          }
        }
        const std::string* variant = p.FindString("variant");
        if (type == NodeType::kButton && variant &&
            (*variant == "alert" || *variant == "accent")) {
          errors.push_back(PartNotDrawn("variant"));
        }
        return true;
      }
      case NodeType::kSelect: {
        if (const base::ListValue* options = p.FindList("options")) {
          for (const base::Value& o : *options) {
            const base::DictValue* od = o.GetIfDict();
            if (od && od->contains("icon")) {
              if (std::optional<std::string> e = IconError(od->Find("icon"))) {
                errors.push_back(*e);
              }
            }
          }
        }
        if (p.contains("label")) {
          errors.push_back(PartNotDrawn("label"));
        }
        return true;
      }
      case NodeType::kSlider:
        if (p.contains("icon") || p.contains("iconAction") ||
            p.contains("toggled")) {
          errors.push_back(PartNotDrawn("icon"));
        }
        return true;
      case NodeType::kSwitch:
        if (p.contains("label")) {
          errors.push_back(PartNotDrawn("label"));
        }
        return true;
      case NodeType::kBadge:
        if (p.contains("role")) {
          errors.push_back(PartNotDrawn("role"));
        }
        return true;
      default:
        return true;
    }
  }

  void BuildRepeat(const UiNode& n,
                   RenderNode& out,
                   bool single_slot,
                   bool in_row) {
    const std::string direction = StringOr(n.props, "direction", "column");
    const bool row = direction == "row";
    out.drawn = single_slot;
    if (single_slot) {
      out.view_class = "views::BoxLayoutView";
      out.layout = row ? "box-horizontal" : "box-vertical";
    }
    const base::DictValue items_binding = n.props.FindDict("items")->Clone();
    out.props.Set("items", items_binding.Clone());
    // The copies sit in this box (one-child slot) or in the parent's layout.
    const bool copies_in_row = single_slot ? row : in_row;
    if (!resolver_->has_snapshot()) {
      out.children.push_back(Build(*n.template_node, false, copies_in_row));
    } else {
      const base::Value items = resolver_->Resolve(items_binding);
      if (!items.is_list()) {
        out.props.Set("count", 0);
        out.errors.push_back(
            base::StrCat({"items does not resolve to an array: ",
                          *items_binding.FindString("$bind")}));
      } else {
        out.props.Set("count", static_cast<int>(items.GetList().size()));
        const base::Value* saved = resolver_->item();
        const std::string* key = n.props.FindString("key");
        base::ListValue keys;
        for (const base::Value& item : items.GetList()) {
          resolver_->set_item(&item);
          if (key) {
            keys.Append(
                resolver_->Resolve(base::DictValue().Set("$bind", *key)));
          }
          out.children.push_back(Build(*n.template_node, false, copies_in_row));
        }
        resolver_->set_item(saved);
        if (key) {
          out.props.Set("keys", std::move(keys));
        }
      }
    }
    if (make_views_ && single_slot) {
      auto box = std::make_unique<views::BoxLayoutView>();
      box->SetOrientation(row ? views::BoxLayout::Orientation::kHorizontal
                              : views::BoxLayout::Orientation::kVertical);
      out.view = box.get();
      out.view_class = ViewClassName(box.get());
      out.owned_view = std::move(box);
      ApplyCommon(out);
      AttachChildren(NodeType::kRepeat, out);
    }
  }

  void BuildOptions(RenderNode& group) {
    const base::ListValue* options = group.props.FindList("options");
    if (!options) {
      return;
    }
    const base::Value* value = group.props.Find("value");
    const base::Value none;
    for (const base::Value& o : *options) {
      const base::DictValue* od = o.GetIfDict();
      if (!od) {
        continue;
      }
      auto option = std::make_unique<RenderNode>();
      option->node = "option";
      option->view_class = "views::RadioButton";
      option->path =
          base::StrCat({group.path, "/options/",
                        base::NumberToString(group.children.size())});
      option->props = od->Clone();
      const base::Value* ov = od->Find("value");
      option->props.Set("checked",
                        (ov ? *ov : none) == (value ? *value : none));
      if (od->contains("icon")) {
        option->errors.push_back(PartNotDrawn("icon"));
      }
      group.children.push_back(std::move(option));
    }
  }

  // Creates the view for a drawn node (not a repeat) and wires its input.
  void CreateView(NodeType type, RenderNode& node, bool in_row) {
    base::WeakPtr<RenderedTree> weak = tree_->GetWeakPtr();
    const RenderNode* np = &node;
    std::unique_ptr<views::View> view;
    switch (type) {
      case NodeType::kColumn:
      case NodeType::kRow:
      case NodeType::kRadioGroup: {
        auto box = std::make_unique<views::BoxLayoutView>();
        box->SetOrientation(type == NodeType::kRow
                                ? views::BoxLayout::Orientation::kHorizontal
                                : views::BoxLayout::Orientation::kVertical);
        view = std::move(box);
        break;
      }
      case NodeType::kStack:
      case NodeType::kSpacer: {
        view = std::make_unique<views::View>();
        if (type == NodeType::kStack) {
          view->SetLayoutManager(std::make_unique<views::FillLayout>());
        }
        break;
      }
      case NodeType::kGrid:
        view = std::make_unique<views::TableLayoutView>();
        break;
      case NodeType::kScroll:
        view = std::make_unique<views::ScrollView>();
        break;
      case NodeType::kLabel:
        view = std::make_unique<views::Label>(std::u16string(),
                                              views::style::CONTEXT_LABEL,
                                              views::style::STYLE_BODY_3);
        break;
      case NodeType::kIcon:
        view = std::make_unique<views::ImageView>();
        break;
      case NodeType::kImage: {
        // CheckNode already loaded it once; a drawn image node decodes.
        auto image = std::make_unique<views::ImageView>();
        ImageLoad load = LoadImage(node.props.Find("src"), options_->plugin_dir,
                                   /*has_snapshot=*/true);
        if (!load.bitmap.isNull()) {
          image->SetImage(ui::ImageModel::FromImageSkia(
              gfx::ImageSkia::CreateFrom1xBitmap(load.bitmap)));
        }
        view = std::move(image);
        break;
      }
      case NodeType::kBadge:
        view = std::make_unique<views::Badge>();
        break;
      case NodeType::kDot: {
        // DotIndicator is only made by Install(parent); take it back out.
        views::View holder;
        views::DotIndicator* dot = views::DotIndicator::Install(&holder);
        std::unique_ptr<views::DotIndicator> owned =
            holder.RemoveChildViewT(dot);
        owned->SetProperty(kViewHookKey, std::unique_ptr<ViewHook>(
                                             std::make_unique<DotHook>(dot)));
        view = std::move(owned);
        break;
      }
      case NodeType::kSeparator: {
        auto separator = std::make_unique<views::Separator>();
        separator->SetOrientation(
            in_row ? views::Separator::Orientation::kVertical
                   : views::Separator::Orientation::kHorizontal);
        view = std::move(separator);
        break;
      }
      case NodeType::kProgress:
        view = std::make_unique<views::ProgressBar>();
        break;
      case NodeType::kButton:
        view = std::make_unique<views::MdTextButton>(
            base::BindRepeating(&Builder::Pressed, weak, base::Unretained(np)));
        break;
      case NodeType::kIconButton:
        view = views::CreateVectorImageButton(
            base::BindRepeating(&Builder::Pressed, weak, base::Unretained(np)));
        break;
      case NodeType::kSwitch:
        view = std::make_unique<views::ToggleButton>(
            base::BindRepeating(&Builder::Toggled, weak, base::Unretained(np)));
        break;
      case NodeType::kCheckbox:
        view = std::make_unique<views::Checkbox>(
            std::u16string(),
            base::BindRepeating(&Builder::Checked, weak, base::Unretained(np)));
        break;
      case NodeType::kSelect: {
        auto combobox = std::make_unique<views::Combobox>();
        combobox->SetCallback(base::BindRepeating(&Builder::Selected, weak,
                                                  base::Unretained(np)));
        view = std::move(combobox);
        break;
      }
      case NodeType::kSlider: {
        auto hook = std::make_unique<SliderHook>(weak, np);
        auto slider = std::make_unique<views::Slider>(hook.get());
        slider->SetProperty(kViewHookKey,
                            std::unique_ptr<ViewHook>(std::move(hook)));
        view = std::move(slider);
        break;
      }
      case NodeType::kTextfield: {
        auto hook = std::make_unique<TextfieldHook>(weak, np);
        auto textfield = std::make_unique<views::Textfield>();
        textfield->SetController(hook.get());
        textfield->SetProperty(kViewHookKey,
                               std::unique_ptr<ViewHook>(std::move(hook)));
        view = std::move(textfield);
        break;
      }
      case NodeType::kListItem:
        view = std::make_unique<ListItemView>(
            base::BindRepeating(&Builder::Pressed, weak, base::Unretained(np)));
        break;
      case NodeType::kRepeat:
      case NodeType::kMarkdown:
      case NodeType::kTile:
      case NodeType::kTabSlider:
      case NodeType::kKeyChips:
      case NodeType::kMediaSession:
        NOTREACHED();
    }
    if (type == NodeType::kGrid) {
      SetUpGridColumns(static_cast<views::TableLayoutView*>(view.get()), node);
    }
    if (type == NodeType::kRadioGroup) {
      CreateOptionViews(node);
    }
    node.view = view.get();
    node.view_class = ViewClassName(view.get());
    node.owned_view = std::move(view);
  }

  void CreateOptionViews(RenderNode& group) {
    base::WeakPtr<RenderedTree> weak = tree_->GetWeakPtr();
    const int group_id = ++group_counter_;
    for (std::unique_ptr<RenderNode>& option : group.children) {
      auto button =
          std::make_unique<views::RadioButton>(std::u16string(), group_id);
      button->SetCallback(base::BindRepeating(&Builder::Radio, weak,
                                              base::Unretained(&group),
                                              base::Unretained(option.get())));
      option->view = button.get();
      option->view_class = ViewClassName(button.get());
      option->owned_view = std::move(button);
      ApplyOption(*option);
    }
  }

  static void SetUpGridColumns(views::TableLayoutView* grid,
                               const RenderNode& node) {
    const int columns = node.props.FindInt("columns").value_or(1);
    const std::string spacing = StringOr(node.props, "spacing", "none");
    const int gap = SpacingOf(spacing, /*horizontal=*/true);
    std::vector<size_t> linked;
    for (int c = 0; c < columns; ++c) {
      if (c > 0 && gap > 0) {
        grid->AddPaddingColumn(views::TableLayout::kFixedSize, gap);
      }
      linked.push_back(static_cast<size_t>(c > 0 && gap > 0 ? 2 * c : c));
      grid->AddColumn(views::LayoutAlignment::kStretch,
                      views::LayoutAlignment::kStretch, 1.0f,
                      views::TableLayout::ColumnSize::kUsePreferred, 0, 0);
    }
    grid->LinkColumnSizes(std::move(linked));
  }

  static void AddGridRows(views::TableLayoutView* grid,
                          const RenderNode& node,
                          size_t views) {
    const int columns = node.props.FindInt("columns").value_or(1);
    const std::string spacing = StringOr(node.props, "spacing", "none");
    const int gap = SpacingOf(spacing, /*horizontal=*/false);
    const size_t rows = (views + columns - 1) / columns;
    for (size_t r = 0; r < rows; ++r) {
      if (r > 0 && gap > 0) {
        grid->AddPaddingRow(views::TableLayout::kFixedSize, gap);
      }
      grid->AddRows(1, views::TableLayout::kFixedSize);
    }
  }

  // Moves the children's views into the node's view.
  void AttachChildren(NodeType type, RenderNode& node) {
    std::vector<RenderNode*> drawn;
    for (std::unique_ptr<RenderNode>& c : node.children) {
      if (c->node == "repeat" && !c->drawn) {
        for (std::unique_ptr<RenderNode>& copy : c->children) {
          drawn.push_back(copy.get());
        }
      } else {
        drawn.push_back(c.get());
      }
    }
    size_t attached = 0;
    for (RenderNode* c : drawn) {
      if (!c->owned_view) {
        continue;
      }
      ++attached;
      switch (type) {
        case NodeType::kScroll:
          static_cast<views::ScrollView*>(node.view.get())
              ->SetContents(std::move(c->owned_view));
          break;
        case NodeType::kListItem:
          static_cast<ListItemView*>(node.view.get())
              ->SetTrailingView(std::move(c->owned_view));
          break;
        case NodeType::kColumn:
        case NodeType::kRow:
        case NodeType::kRadioGroup:
        case NodeType::kRepeat: {
          auto* box = static_cast<views::BoxLayoutView*>(node.view.get());
          views::View* v = box->AddChildView(std::move(c->owned_view));
          if (std::optional<int> flex = c->props.FindInt("flex")) {
            box->SetFlexForView(v, *flex);
          }
          break;
        }
        default:
          node.view->AddChildView(std::move(c->owned_view));
      }
    }
    if (type == NodeType::kGrid) {
      AddGridRows(static_cast<views::TableLayoutView*>(node.view.get()), node,
                  attached);
    }
  }

  // Input callbacks. Each reads the new value from the view and fires the
  // node's action; a dead tree (weak pointer) makes them no-ops.
  static void Pressed(base::WeakPtr<RenderedTree> tree,
                      const RenderNode* node) {
    if (tree) {
      tree->FireAction(node, "action", std::nullopt);
    }
  }
  static void Toggled(base::WeakPtr<RenderedTree> tree,
                      const RenderNode* node) {
    if (tree) {
      tree->FireAction(
          node, "action",
          base::Value(
              static_cast<views::ToggleButton*>(node->view.get())->GetIsOn()));
    }
  }
  static void Checked(base::WeakPtr<RenderedTree> tree,
                      const RenderNode* node) {
    if (tree) {
      tree->FireAction(
          node, "action",
          base::Value(
              static_cast<views::Checkbox*>(node->view.get())->GetChecked()));
    }
  }
  static void Radio(base::WeakPtr<RenderedTree> tree,
                    const RenderNode* group,
                    const RenderNode* option) {
    if (!tree) {
      return;
    }
    const base::Value* value = option->props.Find("value");
    tree->FireAction(group, "action",
                     value ? std::make_optional(value->Clone()) : std::nullopt);
  }
  static void Selected(base::WeakPtr<RenderedTree> tree,
                       const RenderNode* node) {
    if (!tree) {
      return;
    }
    const std::optional<size_t> index =
        static_cast<views::Combobox*>(node->view.get())->GetSelectedIndex();
    const base::ListValue* options = node->props.FindList("options");
    if (!index || !options || *index >= options->size()) {
      return;
    }
    const base::DictValue* option = (*options)[*index].GetIfDict();
    const base::Value* value = option ? option->Find("value") : nullptr;
    tree->FireAction(node, "action",
                     value ? std::make_optional(value->Clone()) : std::nullopt);
  }

  raw_ptr<RenderedTree> tree_;
  raw_ref<BindingResolver> resolver_;
  raw_ref<const RenderOptions> options_;
  const bool make_views_;
  int group_counter_ = 0;
};

void Builder::ApplyCommon(RenderNode& node) {
  views::View* view = node.view;
  const base::DictValue& p = node.props;
  if (std::optional<bool> visible = BoolOf(p.Find("visible"))) {
    view->SetVisible(*visible);
  }
  if (std::optional<bool> enabled = BoolOf(p.Find("enabled"))) {
    view->SetEnabled(*enabled);
  }
  if (std::optional<std::u16string> tooltip = TextOf(p.Find("tooltip"))) {
    if (node.node == "label") {
      static_cast<views::Label*>(view)->SetCustomTooltipText(*tooltip);
    } else {
      view->SetTooltipText(*tooltip);
    }
  }
  if (std::optional<std::u16string> name = TextOf(p.Find("accessibleName"))) {
    if (!name->empty()) {
      view->GetViewAccessibility().SetName(*name);
    }
  }
}

void Builder::ApplyOption(RenderNode& option) {
  auto* button = static_cast<views::RadioButton*>(option.view.get());
  button->SetText(TextOf(option.props.Find("label")).value_or(u""));
  button->SetChecked(option.props.FindBool("checked").value_or(false));
}

void Builder::ApplyProps(NodeType type, RenderNode& node) {
  ApplyCommon(node);
  const base::DictValue& p = node.props;
  views::View* view = node.view;
  const views::LayoutProvider* provider = views::LayoutProvider::Get();
  switch (type) {
    case NodeType::kColumn:
    case NodeType::kRow: {
      auto* box = static_cast<views::BoxLayoutView*>(view);
      box->SetBetweenChildSpacing(
          SpacingOf(StringOr(p, "spacing", "none"), type == NodeType::kRow));
      const std::string align = StringOr(p, "align", "stretch");
      box->SetCrossAxisAlignment(
          align == "start"    ? views::BoxLayout::CrossAxisAlignment::kStart
          : align == "center" ? views::BoxLayout::CrossAxisAlignment::kCenter
          : align == "end"    ? views::BoxLayout::CrossAxisAlignment::kEnd
                              : views::BoxLayout::CrossAxisAlignment::kStretch);
      const std::string container = StringOr(p, "container", "none");
      if (container == "none") {
        box->SetBackground(nullptr);
        box->SetInsideBorderInsets(gfx::Insets());
      } else {
        const float r = CornerRadius();
        const gfx::RoundedCornersF radii =
            container == "rounded-top"      ? gfx::RoundedCornersF(r, r, 0, 0)
            : container == "rounded-bottom" ? gfx::RoundedCornersF(0, 0, r, r)
                                            : gfx::RoundedCornersF(r);
        box->SetBackground(
            views::CreateRoundedRectBackground(ui::kColorSysSurface2, radii));
        box->SetInsideBorderInsets(
            provider->GetInsetsMetric(views::INSETS_DIALOG_SUBSECTION));
      }
      break;
    }
    case NodeType::kScroll: {
      auto* scroll = static_cast<views::ScrollView*>(view);
      // `surface` (and no maxHeight) leaves the ScrollView unclipped.
      const std::string max = StringOr(p, "maxHeight", "surface");
      const int dialog = provider->GetDistanceMetric(
          views::DISTANCE_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT);
      if (max == "small") {
        scroll->ClipHeightTo(0, dialog / 2);
      } else if (max == "medium") {
        scroll->ClipHeightTo(0, dialog);
      } else if (max == "large") {
        scroll->ClipHeightTo(
            0, provider->GetDistanceMetric(
                   views::DISTANCE_MODAL_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT));
      }
      break;
    }
    case NodeType::kLabel: {
      auto* label = static_cast<views::Label*>(view);
      label->SetText(TextOf(p.Find("text")).value_or(u""));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      if (const std::string* typography = p.FindString("typography")) {
        label->SetTextStyle(TextStyleOf(*typography));
      }
      if (std::optional<ui::ColorId> color =
              RoleColor(p.FindString("role"), /*for_icon=*/false)) {
        label->SetEnabledColor(*color);
      }
      const std::string elide = StringOr(p, "elide", "tail");
      label->SetElideBehavior(elide == "middle" ? gfx::ELIDE_MIDDLE
                              : elide == "head" ? gfx::ELIDE_HEAD
                              : elide == "none" ? gfx::NO_ELIDE
                                                : gfx::ELIDE_TAIL);
      if (std::optional<int> lines = p.FindInt("maxLines")) {
        label->SetMultiLine(*lines > 1);
        if (*lines > 1) {
          label->SetMaxLines(static_cast<size_t>(*lines));
        }
      }
      break;
    }
    case NodeType::kIcon: {
      const std::string* name = p.FindString("icon");
      const gfx::VectorIcon* icon = name ? FindIcon(*name) : nullptr;
      CHECK(icon);
      static_cast<views::ImageView*>(view)->SetImage(
          ui::ImageModel::FromVectorIcon(
              *icon,
              RoleColor(p.FindString("role"), /*for_icon=*/true)
                  .value_or(ui::kColorIcon),
              IconSizeOf(StringOr(p, "size", "medium"))));
      break;
    }
    case NodeType::kImage: {
      auto* image = static_cast<views::ImageView*>(view);
      // The plugin directory is not on the node; Rebind keeps the image the
      // build loaded unless src changes (a structure change).
      const std::string size = StringOr(p, "size", "");
      if (!size.empty()) {
        const int unit = IconSizeOf("large");
        const int factor = size == "thumbnail" ? 2
                           : size == "small"   ? 3
                           : size == "medium"  ? 4
                                               : 6;
        image->SetImageSize(gfx::Size(unit * factor, unit * factor));
      }
      if (p.FindBool("rounded").value_or(false)) {
        image->SetPaintToLayer();
        image->layer()->SetFillsBoundsOpaquely(false);
        image->layer()->SetRoundedCornerRadius(
            gfx::RoundedCornersF(CornerRadius()));
      }
      break;
    }
    case NodeType::kBadge:
      static_cast<views::Badge*>(view)->SetText(
          TextOf(p.Find("text")).value_or(u""));
      break;
    case NodeType::kDot: {
      auto* dot = static_cast<views::DotIndicator*>(view);
      const int d =
          provider->GetDistanceMetric(views::DISTANCE_RELATED_LABEL_HORIZONTAL);
      dot->SetPreferredSize(gfx::Size(d, d));
      static_cast<DotHook*>(dot->GetProperty(kViewHookKey))
          ->SetColorId(RoleColor(p.FindString("role"), /*for_icon=*/true));
      if (BoolOf(p.Find("visible")).value_or(true)) {
        dot->Show();
      } else {
        dot->Hide();
      }
      break;
    }
    case NodeType::kSpacer: {
      const int s = SpacingOf(StringOr(p, "size", "none"), true);
      view->SetPreferredSize(gfx::Size(s, s));
      break;
    }
    case NodeType::kProgress:
      static_cast<views::ProgressBar*>(view)->SetValue(
          NumberOf(p.Find("value")).value_or(-1.0));
      break;
    case NodeType::kButton: {
      auto* button = static_cast<views::MdTextButton*>(view);
      button->SetText(TextOf(p.Find("label")).value_or(u""));
      const std::string* name = p.FindString("icon");
      const gfx::VectorIcon* icon = name ? FindIcon(*name) : nullptr;
      button->SetImageModel(
          views::Button::STATE_NORMAL,
          icon ? ui::ImageModel::FromVectorIcon(*icon, ui::kColorIcon,
                                                IconSizeOf("medium"))
               : ui::ImageModel());
      const std::string variant = StringOr(p, "variant", "default");
      button->SetStyle(variant == "primary"     ? ui::ButtonStyle::kProminent
                       : variant == "secondary" ? ui::ButtonStyle::kTonal
                       : variant == "floating"  ? ui::ButtonStyle::kText
                                                : ui::ButtonStyle::kDefault);
      break;
    }
    case NodeType::kIconButton: {
      auto* button = static_cast<views::ImageButton*>(view);
      const std::string* name = p.FindString("icon");
      const gfx::VectorIcon* icon = name ? FindIcon(*name) : nullptr;
      CHECK(icon);
      views::SetImageFromVectorIconWithColor(
          button, *icon, IconSizeOf(StringOr(p, "size", "medium")),
          views::IconColors(ui::kColorIcon, ui::kColorIconDisabled));
      button->SetHighlighted(BoolOf(p.Find("toggled")).value_or(false));
      break;
    }
    case NodeType::kSwitch: {
      auto* toggle = static_cast<views::ToggleButton*>(view);
      toggle->SetIsOn(BoolOf(p.Find("value")).value_or(false));
      if (!p.contains("accessibleName")) {
        std::optional<std::u16string> label = TextOf(p.Find("label"));
        if (label && !label->empty()) {
          toggle->GetViewAccessibility().SetName(*label);
        }
      }
      break;
    }
    case NodeType::kCheckbox: {
      auto* checkbox = static_cast<views::Checkbox*>(view);
      checkbox->SetText(TextOf(p.Find("label")).value_or(u""));
      checkbox->SetChecked(BoolOf(p.Find("value")).value_or(false));
      break;
    }
    case NodeType::kRadioGroup:
      static_cast<views::BoxLayoutView*>(view)->SetBetweenChildSpacing(
          provider->GetDistanceMetric(views::DISTANCE_CONTROL_LIST_VERTICAL));
      break;
    case NodeType::kSelect: {
      auto* combobox = static_cast<views::Combobox*>(view);
      std::vector<ui::SimpleComboboxModel::Item> items;
      std::optional<size_t> selected;
      const base::Value* value = p.Find("value");
      if (const base::ListValue* options = p.FindList("options")) {
        for (const base::Value& o : *options) {
          const base::DictValue* od = o.GetIfDict();
          if (!od) {
            continue;
          }
          const std::string* icon_name = od->FindString("icon");
          const gfx::VectorIcon* icon =
              icon_name ? FindIcon(*icon_name) : nullptr;
          items.emplace_back(
              TextOf(od->Find("label")).value_or(u""), std::u16string(),
              icon ? ui::ImageModel::FromVectorIcon(*icon, ui::kColorIcon,
                                                    IconSizeOf("small"))
                   : ui::ImageModel());
          const base::Value* ov = od->Find("value");
          if (value && ov && *ov == *value) {
            selected = items.size() - 1;
          }
        }
      }
      combobox->SetOwnedModel(
          std::make_unique<ui::SimpleComboboxModel>(std::move(items)));
      combobox->SetSelectedIndex(selected);
      if (!p.contains("accessibleName")) {
        std::optional<std::u16string> label = TextOf(p.Find("label"));
        if (label && !label->empty()) {
          combobox->GetViewAccessibility().SetName(*label);
        }
      }
      break;
    }
    case NodeType::kSlider:
      static_cast<views::Slider*>(view)->SetValue(static_cast<float>(
          std::clamp(NumberOf(p.Find("value")).value_or(0.0), 0.0, 1.0)));
      break;
    case NodeType::kTextfield: {
      auto* textfield = static_cast<views::Textfield*>(view);
      if (std::optional<std::u16string> placeholder =
              TextOf(p.Find("placeholder"))) {
        textfield->SetPlaceholderText(*placeholder);
      }
      if (std::optional<std::u16string> text = TextOf(p.Find("value"))) {
        if (textfield->GetText() != *text) {
          textfield->SetText(*text);
        }
      }
      const std::string size = StringOr(p, "size", "medium");
      textfield->SetDefaultWidthInChars(size == "small"   ? 12
                                        : size == "large" ? 40
                                                          : 24);
      break;
    }
    case NodeType::kListItem: {
      auto* item = static_cast<ListItemView*>(view);
      const std::string* name = p.FindString("icon");
      const gfx::VectorIcon* icon = name ? FindIcon(*name) : nullptr;
      item->SetIcon(icon ? ui::ImageModel::FromVectorIcon(*icon, ui::kColorIcon,
                                                          IconSizeOf("medium"))
                         : ui::ImageModel());
      item->SetLabel(TextOf(p.Find("label")).value_or(u""));
      item->SetSublabel(TextOf(p.Find("sublabel")).value_or(u""));
      item->SetSelected(BoolOf(p.Find("selected")).value_or(false));
      break;
    }
    case NodeType::kStack:
    case NodeType::kGrid:
    case NodeType::kSeparator:
    case NodeType::kRepeat:
    case NodeType::kMarkdown:
    case NodeType::kTile:
    case NodeType::kTabSlider:
    case NodeType::kKeyChips:
    case NodeType::kMediaSession:
      break;
  }
}

bool SameStructure(const RenderNode& a, const RenderNode& b) {
  if (a.node != b.node || a.drawn != b.drawn ||
      a.children.size() != b.children.size()) {
    return false;
  }
  if (a.node == "image" && a.props.Find("src") && b.props.Find("src") &&
      *a.props.Find("src") != *b.props.Find("src")) {
    return false;
  }
  for (size_t i = 0; i < a.children.size(); ++i) {
    if (!SameStructure(*a.children[i], *b.children[i])) {
      return false;
    }
  }
  return true;
}

void Transfer(RenderNode& into, RenderNode& from) {
  into.props = std::move(from.props);
  into.errors = std::move(from.errors);
  for (size_t i = 0; i < into.children.size(); ++i) {
    Transfer(*into.children[i], *from.children[i]);
  }
  if (!into.view) {
    return;
  }
  if (into.node == "option") {
    Builder::ApplyOption(into);
  } else if (into.node == "repeat") {
    // Only a one-child-slot repeat has a view: a box with the common props.
    Builder::ApplyCommon(into);
  } else {
    Builder::ApplyProps(*NodeTypeFromName(into.node), into);
  }
}

void CollectErrors(const RenderNode& node, std::vector<RenderError>* out) {
  for (const std::string& e : node.errors) {
    out->push_back({node.path, e});
  }
  for (const std::unique_ptr<RenderNode>& c : node.children) {
    CollectErrors(*c, out);
  }
}

}  // namespace

UiAction::UiAction() = default;
UiAction::UiAction(UiAction&&) = default;
UiAction& UiAction::operator=(UiAction&&) = default;
UiAction::~UiAction() = default;

RenderOptions::RenderOptions() = default;
RenderOptions::RenderOptions(const RenderOptions&) = default;
RenderOptions& RenderOptions::operator=(const RenderOptions&) = default;
RenderOptions::~RenderOptions() = default;

RenderNode::RenderNode() = default;
RenderNode::~RenderNode() = default;

const gfx::VectorIcon* FindIcon(std::string_view name) {
  for (const IconEntry& entry : IconEntries()) {
    if (entry.name == name) {
      return entry.icon;
    }
  }
  return nullptr;
}

std::vector<IconRow> IconTable() {
  std::vector<IconRow> rows;
  for (const IconEntry& entry : IconEntries()) {
    rows.push_back({entry.name, entry.symbol});
  }
  return rows;
}

RenderedTree::RenderedTree(std::unique_ptr<UiTree> tree,
                           std::optional<Snapshot> snapshot,
                           const RenderOptions& options,
                           UiTreeDelegate* delegate)
    : tree_(std::move(tree)),
      snapshot_(std::move(snapshot)),
      options_(options),
      delegate_(delegate) {}

RenderedTree::~RenderedTree() = default;

std::unique_ptr<views::View> RenderedTree::TakeRootView() {
  return std::move(owned_root_view_);
}

std::vector<RenderError> RenderedTree::errors() const {
  std::vector<RenderError> out;
  CollectErrors(*root_, &out);
  return out;
}

void RenderedTree::OnViewIsDeleting(views::View* observed_view) {
  views_alive_ = false;
  root_view_ = nullptr;
  root_observation_.Reset();
}

RenderedTree::RebindResult RenderedTree::Rebind(Snapshot snapshot) {
  if (root_->drawn && !views_alive_) {
    return RebindResult::kNoViews;
  }
  BindingResolver resolver(&snapshot, options_.clock);
  Builder builder(this, resolver, options_, /*make_views=*/false);
  std::unique_ptr<RenderNode> next =
      builder.Build(*tree_->root, /*single_slot=*/true, /*in_row=*/false);
  if (!SameStructure(*root_, *next)) {
    return RebindResult::kStructureChanged;
  }
  Transfer(*root_, *next);
  snapshot_ = std::move(snapshot);
  return RebindResult::kInPlace;
}

void RenderedTree::FireAction(const RenderNode* node,
                              std::string_view key,
                              std::optional<base::Value> value) {
  if (!delegate_) {
    return;
  }
  const base::DictValue* action = node->props.FindDict(key);
  if (!action) {
    return;
  }
  const std::string* command = action->FindString("command");
  if (!command) {
    LOG(ERROR) << node->path << ": the action's command did not resolve to a "
               << "name, nothing invoked";
    return;
  }
  UiAction out;
  out.command = *command;
  if (const base::DictValue* args = action->FindDict("args")) {
    out.args = args->Clone();
  }
  if (value) {
    out.args.Set("value", std::move(*value));
  }
  if (const std::string* confirm = action->FindString("confirm")) {
    out.confirm = *confirm;
  }
  out.close = action->FindBool("close").value_or(false);
  out.path = node->path;
  delegate_->OnAction(out);
}

std::unique_ptr<RenderedTree> RenderUiTree(std::unique_ptr<UiTree> tree,
                                           std::optional<Snapshot> snapshot,
                                           const RenderOptions& options,
                                           UiTreeDelegate* delegate) {
  CHECK(tree);
  CHECK(tree->root);
  auto rendered = base::WrapUnique(new RenderedTree(
      std::move(tree), std::move(snapshot), options, delegate));
  BindingResolver resolver(
      rendered->snapshot_ ? &*rendered->snapshot_ : nullptr, options.clock);
  Builder builder(rendered.get(), resolver, options, /*make_views=*/true);
  rendered->root_ = builder.Build(*rendered->tree_->root, /*single_slot=*/true,
                                  /*in_row=*/false);
  rendered->owned_root_view_ = std::move(rendered->root_->owned_view);
  rendered->root_view_ = rendered->owned_root_view_.get();
  if (rendered->root_view_) {
    rendered->root_observation_.Observe(rendered->root_view_);
  }
  return rendered;
}

}  // namespace views_shell::ui_tree
