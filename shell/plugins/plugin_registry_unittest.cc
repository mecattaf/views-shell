// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/plugin_registry.h"

#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/plugins/registry_view.h"

namespace views_shell {
namespace {

base::FilePath Data(std::string_view relative) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data").AppendASCII(relative);
}

void WritePlugin(const base::FilePath& dir, std::string_view id) {
  ASSERT_TRUE(base::CreateDirectory(dir));
  ASSERT_TRUE(base::WriteFile(
      dir.AppendASCII(kPluginManifestFileName),
      base::StrCat({R"({"schemaVersion": 1, "id": ")", id,
                    R"(", "name": "t", "version": "0.1.0",
        "engines": {"views-shell": ">=0.1.0"},
        "runtime": {"mode": "declarative"},
        "contributes": {
          "commands": [{"id": "go", "title": "Go",
                        "handler": {"open": "settings"}}],
          "keybindings": [{"command": "go", "key": "F9"}],
          "cli": {"name": "go", "verbs": [{"verb": "run", "command": "go"}]}
        }})"})));
}

// Rule R14: a plugin whose required capability is missing is not registered.
TEST(PluginRegistryTest, RequiredCapabilityGatesRegistration) {
  PluginRegistry without(CapabilitySet{"workspaces.focus"});
  without.Discover({Data("examples/music-scratchpad")});
  EXPECT_TRUE(without.plugins().empty());
  ASSERT_EQ(without.rejected().size(), 1u);
  EXPECT_EQ(without.rejected()[0].kind,
            RegistryRejection::Kind::kMissingCapability);
  EXPECT_EQ(without.rejected()[0].plugin_id, "example.music-scratchpad");
  EXPECT_EQ(without.rejected()[0].reason,
            "missing required capability scratchpad.toggle (rule R14)");

  // The optional scroll.lua is not needed to register.
  PluginRegistry with(CapabilitySet{"scratchpad.toggle"});
  with.Discover({Data("examples/music-scratchpad")});
  ASSERT_EQ(with.plugins().size(), 1u);
  EXPECT_TRUE(with.rejected().empty());
  EXPECT_TRUE(with.StartsAtStartup("example.music-scratchpad"));
  std::string golden;
  ASSERT_TRUE(base::ReadFileToString(
      Data("fixtures/registry/example.music-scratchpad.json"), &golden));
  EXPECT_EQ(WriteRegistryJson(
                base::Value(with.ViewOf("example.music-scratchpad")->Clone())),
            golden);
}

TEST(PluginRegistryTest, DiscoversEveryExampleWithTheirCapabilities) {
  CapabilitySet all;
  for (const std::string_view dir :
       {"examples/music-scratchpad", "examples/power-menu",
        "examples/workspace-rail"}) {
    base::expected<PluginManifest, std::string> m =
        LoadPluginManifest(Data(dir));
    ASSERT_TRUE(m.has_value()) << m.error();
    for (const Capability& c : m->required_capabilities) {
      all.insert(c);
    }
  }
  PluginRegistry registry(all);
  registry.Discover({Data("examples")});
  EXPECT_EQ(registry.plugins().size(), 12u);
  EXPECT_TRUE(registry.rejected().empty());
  ASSERT_TRUE(registry.Find("example.echo"));
  EXPECT_EQ(registry.Find("example.echo")->tier, PluginTier::kProcess);
  EXPECT_TRUE(registry.StartsAtStartup("example.echo"));

  // Dependencies on source plugins that are not installed are recorded.
  bool dnd_dependency = false;
  for (const RegistryConflict& conflict : registry.conflicts()) {
    if (conflict.kind == RegistryConflict::Kind::kMissingDependency &&
        conflict.key == "views-shell.notifications") {
      dnd_dependency =
          conflict.plugins == std::vector<std::string>{"views-shell.qs-dnd"};
    }
    // views-shell.network is among the examples, so qs-network resolves.
    EXPECT_NE(conflict.key, "views-shell.network");
  }
  EXPECT_TRUE(dnd_dependency);
}

TEST(PluginRegistryTest, DuplicateIdsAndSharedKeysAreRecorded) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  WritePlugin(temp.GetPath().AppendASCII("a"), "test.first");
  WritePlugin(temp.GetPath().AppendASCII("b"), "test.first");
  WritePlugin(temp.GetPath().AppendASCII("c"), "test.second");
  ASSERT_TRUE(base::CreateDirectory(temp.GetPath().AppendASCII("d")));
  ASSERT_TRUE(base::WriteFile(
      temp.GetPath().AppendASCII("d").AppendASCII(kPluginManifestFileName),
      "{not json"));

  PluginRegistry registry{CapabilitySet()};
  registry.Discover({temp.GetPath()});
  ASSERT_EQ(registry.plugins().size(), 2u);
  ASSERT_EQ(registry.rejected().size(), 2u);
  EXPECT_EQ(registry.rejected()[0].kind,
            RegistryRejection::Kind::kDuplicateId);
  EXPECT_EQ(registry.rejected()[0].dir, temp.GetPath().AppendASCII("b"));
  EXPECT_EQ(registry.rejected()[1].kind, RegistryRejection::Kind::kInvalid);
  EXPECT_TRUE(registry.rejected()[1].reason.starts_with("not JSON"));

  ASSERT_EQ(registry.conflicts().size(), 2u);
  EXPECT_EQ(registry.conflicts()[0].kind,
            RegistryConflict::Kind::kKeybinding);
  EXPECT_EQ(registry.conflicts()[0].key, "F9");
  EXPECT_EQ(registry.conflicts()[0].plugins,
            (std::vector<std::string>{"test.first", "test.second"}));
  EXPECT_EQ(registry.conflicts()[1].kind, RegistryConflict::Kind::kCliName);
  EXPECT_EQ(registry.conflicts()[1].key, "go");
}

}  // namespace
}  // namespace views_shell
