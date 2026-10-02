// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Bindings and formats, one test per format, with the values the reference
// (tools/ui-tree-render.py) gives for the same input. No display, no Views.

#include "views_shell/ui_tree/binding.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell::ui_tree {
namespace {

base::Value Json(std::string_view text) {
  std::optional<base::Value> v =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  CHECK(v) << text;
  return std::move(*v);
}

std::string Format(std::string_view format, std::string_view value_json) {
  base::Value out = ApplyFormat(*ParseFormat(format), Json(value_json),
                                FormatClock::ForTrace());
  CHECK(out.is_string());
  return out.GetString();
}

TEST(BindingFormatTest, Percent) {
  EXPECT_EQ(Format("percent", "0.855"), "86%");
  EXPECT_EQ(Format("percent", "0.7"), "70%");
  EXPECT_EQ(Format("percent", "0.005"), "1%");
  EXPECT_EQ(Format("percent", "-0.005"), "-1%");
  EXPECT_EQ(Format("percent", "1"), "100%");
  // A value of the wrong kind renders as text.
  EXPECT_EQ(Format("percent", "true"), "true");
  EXPECT_EQ(Format("percent", "\"half\""), "half");
}

TEST(BindingFormatTest, Duration) {
  EXPECT_EQ(Format("duration", "93784"), "1 d 2 h");
  EXPECT_EQ(Format("duration", "3725"), "1 h 2 min");
  EXPECT_EQ(Format("duration", "125.9"), "2 min");
  EXPECT_EQ(Format("duration", "59.99"), "59 s");
  EXPECT_EQ(Format("duration", "0"), "0 s");
  // Python's divmod floors: -5 s is -1 d 23 h.
  EXPECT_EQ(Format("duration", "-5"), "-1 d 23 h");
}

TEST(BindingFormatTest, Bytes) {
  EXPECT_EQ(Format("bytes", "1023"), "1023 B");
  EXPECT_EQ(Format("bytes", "1536"), "1.5 KB");
  EXPECT_EQ(Format("bytes", "536870912"), "512 MB");
  EXPECT_EQ(Format("bytes", "1048575"), "1024 KB");
  EXPECT_EQ(Format("bytes", "102348"), "99.9 KB");
  EXPECT_EQ(Format("bytes", "1.2e18"), "1066 PB");
}

TEST(BindingFormatTest, Time) {
  // 2026-10-02T13:45:10Z as epoch seconds, then as ISO 8601 in two offsets.
  EXPECT_EQ(Format("time", "1790948710"), "13:45");
  EXPECT_EQ(Format("time", "\"2026-10-02T13:45:10Z\""), "13:45");
  EXPECT_EQ(Format("time", "\"2026-10-02T15:45:10.5+02:00\""), "13:45");
  EXPECT_EQ(Format("time", "\"2026-10-02T08:45-0500\""), "13:45");
  // Not an instant (no offset, or no time): rendered as text.
  EXPECT_EQ(Format("time", "\"2026-10-02T13:45:10\""), "2026-10-02T13:45:10");
  EXPECT_EQ(Format("time", "\"2026-10-02\""), "2026-10-02");
  EXPECT_EQ(Format("time", "\"2026-02-30T00:00Z\""), "2026-02-30T00:00Z");
}

TEST(BindingFormatTest, RelativeTime) {
  // Against the trace clock, 2026-10-02T00:00:00Z.
  EXPECT_EQ(Format("relative-time", "\"2026-10-01T23:59:30Z\""), "now");
  EXPECT_EQ(Format("relative-time", "\"2026-10-01T23:55:00Z\""), "5 min ago");
  EXPECT_EQ(Format("relative-time", "\"2026-10-01T21:00:00Z\""), "3 h ago");
  EXPECT_EQ(Format("relative-time", "\"2026-09-29T00:00:00Z\""), "3 d ago");
  EXPECT_EQ(Format("relative-time", "\"2026-10-02T02:30:00Z\""), "in 2 h");
  EXPECT_EQ(Format("relative-time", "\"2026-10-02T00:00:59Z\""), "now");
  EXPECT_EQ(Format("relative-time", "\"2026-10-02T00:01:01Z\""), "in 1 min");
}

TEST(BindingFormatTest, Text) {
  EXPECT_EQ(Format("text", "null"), "");
  EXPECT_EQ(Format("text", "false"), "false");
  EXPECT_EQ(Format("text", "42"), "42");
  EXPECT_EQ(Format("text", "3.0"), "3");
  EXPECT_EQ(Format("text", "0.1"), "0.1");
  EXPECT_EQ(Format("text", "1e-07"), "1e-07");
  EXPECT_EQ(Format("text", R"({"b":[1,2.5],"a":"é"})"),
            R"({"a":"é","b":[1,2.5]})");
}

TEST(BindingFormatTest, PythonFloatRepr) {
  EXPECT_EQ(PythonFloatRepr(0.62), "0.62");
  EXPECT_EQ(PythonFloatRepr(1.0), "1.0");
  EXPECT_EQ(PythonFloatRepr(100.0), "100.0");
  EXPECT_EQ(PythonFloatRepr(0.0001), "0.0001");
  EXPECT_EQ(PythonFloatRepr(0.00001), "1e-05");
  EXPECT_EQ(PythonFloatRepr(1.5e16), "1.5e+16");
  EXPECT_EQ(PythonFloatRepr(123456789012345.6), "123456789012345.6");
  EXPECT_EQ(PythonFloatRepr(-2.5), "-2.5");
}

TEST(BindingFormatTest, PythonJson) {
  const base::Value v =
      Json(R"({"z":{},"a":[],"m":[1,{"k":null}],"s":"q\"\n"})");
  EXPECT_EQ(ToPythonJson(v, /*pretty=*/true),
            "{\n  \"a\": [],\n  \"m\": [\n    1,\n    {\n      \"k\": null\n"
            "    }\n  ],\n  \"s\": \"q\\\"\\n\",\n  \"z\": {}\n}");
  EXPECT_EQ(ToPythonJson(v, /*pretty=*/false),
            R"({"a":[],"m":[1,{"k":null}],"s":"q\"\n","z":{}})");
}

TEST(BindingTest, PointerFollowsRfc6901) {
  const base::Value doc = Json(R"({"a/b":{"~x":[10,20]},"":7,"l":[0,1]})");
  ASSERT_TRUE(EvaluatePointer(doc, "/a~1b/~0x/1"));
  EXPECT_EQ(*EvaluatePointer(doc, "/a~1b/~0x/1"), base::Value(20));
  EXPECT_EQ(*EvaluatePointer(doc, "/"), base::Value(7));
  EXPECT_EQ(EvaluatePointer(doc, ""), &doc);
  EXPECT_FALSE(EvaluatePointer(doc, "/l/01"));
  EXPECT_FALSE(EvaluatePointer(doc, "/l/2"));
  EXPECT_FALSE(EvaluatePointer(doc, "/l/-"));
  EXPECT_FALSE(EvaluatePointer(doc, "/missing"));
}

TEST(BindingTest, ResolvesStateItemsAndSources) {
  Snapshot snapshot = Snapshot::FromJson(Json(R"({
      "snapshot": {"battery": {"fraction": 0.7}, "n": null},
      "sources": {"views-shell.power/profiles": {"profile": "Balanced"}}})"))
                          .value();
  BindingResolver resolver(&snapshot, FormatClock::ForTrace());
  auto bind = [&](std::string_view json) {
    return resolver.ResolveValue(Json(json));
  };
  EXPECT_EQ(bind(R"({"$bind":"/battery/fraction","format":"percent"})"),
            base::Value("70%"));
  EXPECT_EQ(
      bind(R"({"$bind":"/profile","source":"views-shell.power/profiles"})"),
      base::Value("Balanced"));
  // null resolves; a missing key does not.
  EXPECT_EQ(bind(R"({"$bind":"/n"})"), base::Value());
  EXPECT_EQ(bind(R"({"$bind":"/nope"})"), Json(R"({"$unresolved":"/nope"})"));
  // ./ reads the current repeat item; outside a repeat it does not resolve.
  EXPECT_EQ(bind(R"({"$bind":"./id"})"), Json(R"({"$unresolved":"./id"})"));
  const base::Value item = Json(R"({"id":"lock"})");
  resolver.set_item(&item);
  EXPECT_EQ(bind(R"({"$bind":"./id"})"), base::Value("lock"));
  EXPECT_EQ(bind(R"({"$bind":"."})"), item);
  // Containers are walked.
  EXPECT_EQ(bind(R"({"device":{"$bind":"./id"},"n":[{"$bind":"/n"}]})"),
            Json(R"({"device":"lock","n":[null]})"));
}

TEST(BindingTest, WithoutSnapshotBindingsStayAsWritten) {
  BindingResolver resolver(nullptr, FormatClock::ForTrace());
  const base::Value b = Json(R"({"$bind":"/a","format":"percent"})");
  EXPECT_EQ(resolver.ResolveValue(b), b);
}

}  // namespace
}  // namespace views_shell::ui_tree
