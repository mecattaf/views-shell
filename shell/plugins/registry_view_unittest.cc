// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/registry_view.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {
namespace {

base::FilePath Data(std::string_view relative) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data").AppendASCII(relative);
}

// Every example's C++ registry view is byte for byte the committed golden
// that tools/plugin-registry.py writes and checks.
TEST(PluginRegistryViewTest, EveryExampleMatchesItsGoldenByteForByte) {
  base::FileEnumerator walk(Data("examples"), false,
                            base::FileEnumerator::DIRECTORIES);
  std::vector<base::FilePath> dirs;
  for (base::FilePath dir = walk.Next(); !dir.empty(); dir = walk.Next()) {
    if (base::PathExists(dir.AppendASCII(kPluginManifestFileName))) {
      dirs.push_back(dir);
    }
  }
  std::sort(dirs.begin(), dirs.end());
  ASSERT_EQ(dirs.size(), 12u);
  for (const base::FilePath& dir : dirs) {
    base::expected<PluginManifest, std::string> manifest =
        LoadPluginManifest(dir);
    ASSERT_TRUE(manifest.has_value()) << manifest.error();
    const std::string view =
        WriteRegistryJson(base::Value(BuildRegistryView(*manifest)));
    std::string golden;
    ASSERT_TRUE(base::ReadFileToString(
        Data("fixtures/registry").AppendASCII(manifest->id + ".json"),
        &golden))
        << manifest->id;
    EXPECT_EQ(view, golden) << manifest->id;
    // And canonically: the same JSON value.
    EXPECT_EQ(base::JSONReader::Read(view, base::JSON_PARSE_RFC),
              base::JSONReader::Read(golden, base::JSON_PARSE_RFC))
        << manifest->id;
  }
}

TEST(PluginRegistryViewTest, WriterFormatsLikePythonJsonDumps) {
  std::optional<base::Value> value = base::JSONReader::Read(
      R"({"b": [], "a": {}, "c": [1, 2.5, 3.0, null, true, "é\n\"\u0001"],
          "d": {"z": 1, "y": {"x": false}}})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(value);
  EXPECT_EQ(WriteRegistryJson(*value),
            "{\n"
            "  \"a\": {},\n"
            "  \"b\": [],\n"
            "  \"c\": [\n"
            "    1,\n"
            "    2.5,\n"
            "    3.0,\n"
            "    null,\n"
            "    true,\n"
            "    \"\xc3\xa9\\n\\\"\\u0001\"\n"
            "  ],\n"
            "  \"d\": {\n"
            "    \"y\": {\n"
            "      \"x\": false\n"
            "    },\n"
            "    \"z\": 1\n"
            "  }\n"
            "}\n");
}

TEST(PluginRegistryViewTest, ActivationIsInferredFromContributions) {
  base::expected<PluginManifest, std::string> echo =
      LoadPluginManifest(Data("examples/echo-process"));
  ASSERT_TRUE(echo.has_value()) << echo.error();
  const base::DictValue view = BuildRegistryView(*echo);
  const base::ListValue* activation = view.FindList("activation");
  ASSERT_TRUE(activation);
  std::vector<std::string> events;
  for (const base::Value& event : *activation) {
    events.push_back(event.GetString());
  }
  EXPECT_EQ(events, (std::vector<std::string>{
                        "onCommand:example.echo/ping",
                        "onCommand:example.echo/set-counter",
                        "onCommand:example.echo/try-exec",
                        "onEvent:workspace",
                        "onStartup",
                        "onSurface:panel",
                    }));
}

}  // namespace
}  // namespace views_shell
