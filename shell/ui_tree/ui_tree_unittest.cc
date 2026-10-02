// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The parser against the repository's own trees: every examples/*/ui/*.json
// parses, every tools/fixtures/invalid/*.ui.json is rejected (the same files
// tools/validate.py rejects with the JSON Schema), and errors carry the JSON
// path of what was wrong. No display, no Views.

#include "views_shell/ui_tree/ui_tree.h"

#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell::ui_tree {
namespace {

base::FilePath DataRoot() {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data");
}

std::vector<base::FilePath> Files(const base::FilePath& dir,
                                  const std::string& pattern,
                                  bool recursive) {
  std::vector<base::FilePath> out;
  base::FileEnumerator e(dir, recursive, base::FileEnumerator::FILES, pattern);
  for (base::FilePath p = e.Next(); !p.empty(); p = e.Next()) {
    out.push_back(p);
  }
  return out;
}

ParseError ParseFails(const std::string& json) {
  auto result = ParseUiTreeJson(json);
  EXPECT_FALSE(result.has_value()) << json;
  return result.has_value() ? ParseError{} : result.error();
}

TEST(UiTreeParseTest, EveryExampleParses) {
  int count = 0;
  for (const base::FilePath& dir : {DataRoot().AppendASCII("examples")}) {
    base::FileEnumerator plugins(dir, false, base::FileEnumerator::DIRECTORIES);
    for (base::FilePath plugin = plugins.Next(); !plugin.empty();
         plugin = plugins.Next()) {
      for (const base::FilePath& file :
           Files(plugin.AppendASCII("ui"), "*.json", false)) {
        std::string json;
        ASSERT_TRUE(base::ReadFileToString(file, &json)) << file;
        auto tree = ParseUiTreeJson(json);
        ASSERT_TRUE(tree.has_value())
            << file << ": " << tree.error().ToString();
        EXPECT_TRUE((*tree)->root);
        ++count;
      }
    }
  }
  // The examples hold 14 ui files (tools/fixtures/render has one golden each).
  EXPECT_EQ(count, 14);
}

TEST(UiTreeParseTest, EveryInvalidFixtureIsRejected) {
  const std::vector<base::FilePath> bad =
      Files(DataRoot().AppendASCII("fixtures/invalid"), "*.ui.json", false);
  ASSERT_EQ(bad.size(), 4u);
  for (const base::FilePath& file : bad) {
    std::string json;
    ASSERT_TRUE(base::ReadFileToString(file, &json));
    auto tree = ParseUiTreeJson(json);
    EXPECT_FALSE(tree.has_value()) << file << " was accepted";
  }
}

TEST(UiTreeParseTest, ErrorsNameTheJsonPath) {
  EXPECT_EQ(
      ParseFails(R"({"schemaVersion":1,"root":{"type":"webview"}})").ToString(),
      "/root/type: unknown node type \"webview\"");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"column",
      "children":[{"type":"label","text":"a","color":"#ff0000"}]}})")
                .path,
            "/root/children/0/color");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"mediaSession"}})")
                .ToString(),
            "/root: mediaSession needs \"session\"");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"label",
      "text":{"$bind":"x"}}})")
                .path,
            "/root/text/$bind");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"button",
      "label":"a","action":{"command":"Bad Name"}}})")
                .path,
            "/root/action/command");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"grid",
      "columns":7}})")
                .path,
            "/root/columns");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":2,"root":{"type":"spacer"}})").path,
            "/schemaVersion");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"label",
      "text":"a","role":"red"}})")
                .path,
            "/root/role");
  EXPECT_EQ(ParseFails(R"({"schemaVersion":1,"root":{"type":"repeat",
      "items":{"$bind":"/a"},"template":{"type":"label","text":"x",
      "tooltip":{"$bind":"./t","source":"bad"}}}})")
                .path,
            "/root/template/tooltip/source");
}

TEST(UiTreeParseTest, TypedSlots) {
  auto tree = ParseUiTreeJson(R"({"schemaVersion":1,"root":{"type":"column",
      "children":[
        {"type":"scroll","child":{"type":"repeat","items":{"$bind":"/l"},
          "template":{"type":"listItem","label":"x",
            "trailing":{"type":"dot"}}}},
        {"type":"separator"}]}})");
  ASSERT_TRUE(tree.has_value()) << tree.error().ToString();
  const UiNode& root = *(*tree)->root;
  EXPECT_EQ(root.type, NodeType::kColumn);
  ASSERT_EQ(root.children.size(), 2u);
  const UiNode& scroll = *root.children[0];
  EXPECT_EQ(scroll.type, NodeType::kScroll);
  ASSERT_TRUE(scroll.child);
  EXPECT_EQ(scroll.child->type, NodeType::kRepeat);
  ASSERT_TRUE(scroll.child->template_node);
  EXPECT_EQ(scroll.child->template_node->type, NodeType::kListItem);
  ASSERT_TRUE(scroll.child->template_node->trailing);
  EXPECT_EQ(scroll.child->template_node->trailing->path,
            "/root/children/0/child/template/trailing");
  // Node-valued keys are slots, never props.
  EXPECT_FALSE(scroll.props.contains("child"));
  EXPECT_FALSE(root.props.contains("children"));
  EXPECT_EQ(NodeTypeName(NodeType::kIconButton), "iconButton");
  EXPECT_EQ(NodeTypeFromName("tabSlider"), NodeType::kTabSlider);
  EXPECT_FALSE(NodeTypeFromName("webview"));
}

}  // namespace
}  // namespace views_shell::ui_tree
