// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/plugin_manifest.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell {
namespace {

// //views_shell/data, which tools/bench/worker/wire.sh fills from the
// repository's examples/ and tools/fixtures/.
base::FilePath Data(std::string_view relative) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data").AppendASCII(relative);
}

std::vector<base::FilePath> ExampleDirs() {
  std::vector<base::FilePath> dirs;
  base::FileEnumerator walk(Data("examples"), false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = walk.Next(); !dir.empty(); dir = walk.Next()) {
    if (base::PathExists(dir.AppendASCII(kPluginManifestFileName))) {
      dirs.push_back(dir);
    }
  }
  std::sort(dirs.begin(), dirs.end());
  return dirs;
}

void WriteText(const base::FilePath& path, std::string_view text) {
  ASSERT_TRUE(base::CreateDirectory(path.DirName()));
  ASSERT_TRUE(base::WriteFile(path, text));
}

TEST(PluginManifestTest, EveryExampleManifestIsAccepted) {
  const std::vector<base::FilePath> dirs = ExampleDirs();
  EXPECT_EQ(dirs.size(), 12u);
  for (const base::FilePath& dir : dirs) {
    base::expected<PluginManifest, std::string> manifest =
        LoadPluginManifest(dir);
    EXPECT_TRUE(manifest.has_value())
        << dir.BaseName().value() << ": "
        << (manifest.has_value() ? std::string() : manifest.error());
  }
}

// tools/fixtures/registry/rejects.json is the reason tools/plugin-registry.py
// prints for each invalid fixture; the C++ checks must give the same string.
TEST(PluginManifestTest, EveryInvalidFixtureIsRejectedWithPythonsReason) {
  std::string text;
  ASSERT_TRUE(
      base::ReadFileToString(Data("fixtures/registry/rejects.json"), &text));
  std::optional<base::Value> reasons =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(reasons && reasons->is_dict());

  base::FileEnumerator walk(Data("fixtures/invalid"), false,
                            base::FileEnumerator::FILES, "*.json");
  size_t checked = 0;
  for (base::FilePath file = walk.Next(); !file.empty(); file = walk.Next()) {
    const std::string name = file.BaseName().value();
    if (name.ends_with(".ui.json")) {
      continue;  // ui-tree fixtures belong to the renderer's validator
    }
    ++checked;
    base::expected<PluginManifest, std::string> manifest =
        LoadPluginManifest(file);
    ASSERT_FALSE(manifest.has_value()) << name << " was accepted";
    const std::string* want = reasons->GetDict().FindString(name);
    ASSERT_TRUE(want) << name << " has no reason in rejects.json";
    EXPECT_EQ(manifest.error(), *want) << name;
  }
  EXPECT_EQ(checked, 18u);
  EXPECT_EQ(reasons->GetDict().size(), checked);
}

TEST(PluginManifestTest, PyReprMatchesPython) {
  std::optional<base::Value> value = base::JSONReader::Read(
      R"({"b": 1, "a": [true, null, 1.5, 2.0, "it's", "q\"'", "back\\slash"]})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(value);
  EXPECT_EQ(PyRepr(*value),
            R"({'a': [True, None, 1.5, 2.0, "it's", 'q"\'', 'back\\slash'], 'b': 1})");
}

TEST(PluginManifestTest, SchemaProblemsSortByInstancePath) {
  std::optional<base::Value> value = base::JSONReader::Read(
      R"({"schemaVersion": 1, "id": "Bad", "name": "", "version": "1",
          "engines": {"views-shell": ">=0.1.0"},
          "runtime": {"mode": "declarative"}, "extra": true})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(value);
  std::vector<ManifestProblem> problems = CheckManifestSchema(*value);
  std::vector<std::string> reasons;
  for (const ManifestProblem& problem : problems) {
    reasons.push_back(problem.ToReason());
  }
  EXPECT_EQ(reasons,
            (std::vector<std::string>{
                "(root): Additional properties are not allowed ('extra' was "
                "unexpected)",
                R"(id: 'Bad' does not match '^[a-z0-9][a-z0-9-]*(\\.[a-z0-9][a-z0-9-]*)+$')",
                "name: '' should be non-empty",
                R"(version: '1' does not match '^(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$')",
            }));
}

TEST(PluginManifestTest, CrossFileChecksMatchValidatePy) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath dir = temp.GetPath().AppendASCII("p");
  WriteText(dir.AppendASCII(kPluginManifestFileName), R"({
    "schemaVersion": 1, "id": "test.cross", "name": "x", "version": "0.1.0",
    "engines": {"views-shell": ">=0.1.0"}, "runtime": {"mode": "declarative"},
    "contributes": {
      "surfaces": [{"id": "panel", "kind": "panel", "ui": "ui/panel.json"}],
      "keybindings": [{"command": "missing", "key": "F8"}]
    }
  })");
  base::expected<PluginManifest, std::string> manifest =
      LoadPluginManifest(dir);
  ASSERT_FALSE(manifest.has_value());
  EXPECT_EQ(manifest.error(), "names missing file ui/panel.json");

  WriteText(dir.AppendASCII("ui/panel.json"), R"({"schemaVersion": 1,
    "root": {"type": "button", "label": "x",
             "action": {"command": "other.plugin/run"}}})");
  manifest = LoadPluginManifest(dir);
  ASSERT_FALSE(manifest.has_value());
  EXPECT_EQ(manifest.error(), "references undeclared command missing");

  WriteText(dir.AppendASCII(kPluginManifestFileName), R"({
    "schemaVersion": 1, "id": "test.cross", "name": "x", "version": "0.1.0",
    "engines": {"views-shell": ">=0.1.0"}, "runtime": {"mode": "declarative"},
    "contributes": {
      "surfaces": [{"id": "panel", "kind": "panel", "ui": "ui/panel.json"}]
    }
  })");
  manifest = LoadPluginManifest(dir);
  ASSERT_FALSE(manifest.has_value());
  EXPECT_EQ(manifest.error(),
            "calls other.plugin/run without a call: permission");
}

TEST(PluginManifestTest, BuildsTheTypedManifest) {
  base::expected<PluginManifest, std::string> echo =
      LoadPluginManifest(Data("examples/echo-process"));
  ASSERT_TRUE(echo.has_value()) << echo.error();
  EXPECT_EQ(echo->id, "example.echo");
  EXPECT_EQ(echo->tier, PluginTier::kProcess);
  EXPECT_EQ(echo->protocol, 1);
  EXPECT_EQ(echo->exec, "plugin.py");
  EXPECT_TRUE(echo->HasPermission("notifications"));
  EXPECT_EQ(echo->TreeSurfaces(),
            (std::vector<std::string>{"button", "panel"}));
  EXPECT_EQ(echo->events, (std::vector<std::string>{"workspace"}));
  EXPECT_EQ(*echo->config_defaults.FindString("greeting"), "hello");
  const PluginCommand* ping = echo->FindCommand("example.echo/ping");
  ASSERT_TRUE(ping);
  EXPECT_EQ(ping->result, "json");
  ASSERT_EQ(ping->args.size(), 1u);
  EXPECT_TRUE(ping->args[0].required);
  EXPECT_FALSE(echo->FindCommand("other.plugin/ping"));
}

}  // namespace
}  // namespace views_shell
