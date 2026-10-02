// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/json_rpc.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell {
namespace {

TEST(JsonRpcTest, LinesSurviveReadsThatEndAnywhere) {
  const std::string stream =
      "{\"jsonrpc\":\"2.0\",\"method\":\"snapshot\",\"params\":{\"data\":"
      "{\"t\":\"\xc3\xa9\"}}}\n\n   \n"
      "{\"jsonrpc\":\"2.0\",\"id\":\"host-1\",\"result\":{}}\r\n";
  // One byte at a time: every split point, including inside the UTF-8 é.
  JsonRpcLineReader reader;
  std::vector<std::string> lines;
  for (char c : stream) {
    for (std::string& line : reader.Append(std::string(1, c))) {
      lines.push_back(std::move(line));
    }
  }
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1], "{\"jsonrpc\":\"2.0\",\"id\":\"host-1\",\"result\":{}}");
  EXPECT_TRUE(reader.TakeRemainder().empty());

  // The same stream in one chunk gives the same lines.
  JsonRpcLineReader whole;
  EXPECT_EQ(whole.Append(stream), lines);

  // An unterminated tail is kept for end of stream.
  JsonRpcLineReader tail;
  EXPECT_TRUE(tail.Append("{\"partial\"").empty());
  EXPECT_EQ(tail.TakeRemainder(), "{\"partial\"");
}

TEST(JsonRpcTest, ClassifiesTheFourShapes) {
  auto request = ParseJsonRpcLine(
      R"({"jsonrpc":"2.0","id":"echo-1","method":"exec","params":{"program":"true"}})");
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->kind, JsonRpcMessage::Kind::kRequest);
  EXPECT_EQ(request->method, "exec");
  EXPECT_EQ(*request->params.FindString("program"), "true");
  EXPECT_EQ(JsonRpcIdToString(request->id), "echo-1");

  auto notification =
      ParseJsonRpcLine(R"({"jsonrpc":"2.0","method":"snapshot"})");
  ASSERT_TRUE(notification.has_value());
  EXPECT_EQ(notification->kind, JsonRpcMessage::Kind::kNotification);
  EXPECT_TRUE(notification->params.empty());

  auto result = ParseJsonRpcLine(R"({"jsonrpc":"2.0","id":7,"result":[1]})");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->kind, JsonRpcMessage::Kind::kResult);
  EXPECT_EQ(JsonRpcIdToString(result->id), "7");
  EXPECT_TRUE(result->result.is_list());

  auto error = ParseJsonRpcLine(
      R"({"jsonrpc":"2.0","id":"host-2","error":{"code":-32001,"message":"permission not declared","data":{"permission":"exec:true"}}})");
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->kind, JsonRpcMessage::Kind::kError);
  EXPECT_EQ(error->error.code, kJsonRpcPermissionNotDeclared);
  EXPECT_EQ(*error->error.data.GetDict().FindString("permission"),
            "exec:true");
}

TEST(JsonRpcTest, MalformedLinesAreDroppedWithAReason) {
  for (const char* line : {
           "{this is not json",
           "[1,2]",
           R"({"id":1,"method":"x"})",
           R"({"jsonrpc":"1.0","method":"x"})",
           R"({"jsonrpc":"2.0","id":1})",
           R"({"jsonrpc":"2.0","id":1,"result":{},"error":{"code":1,"message":"m"}})",
           R"({"jsonrpc":"2.0","id":1,"error":{"code":"x","message":"m"}})",
           R"({"jsonrpc":"2.0","method":"x","params":[1]})",
           R"({"jsonrpc":"2.0","method":7})",
           R"({"jsonrpc":"2.0","id":{},"method":"x"})",
       }) {
    auto parsed = ParseJsonRpcLine(line);
    EXPECT_FALSE(parsed.has_value()) << line;
    if (!parsed.has_value()) {
      EXPECT_FALSE(parsed.error().empty());
    }
  }
}

TEST(JsonRpcTest, SerializeWritesOneLineThatParsesBack) {
  JsonRpcMessage request = JsonRpcMessage::Request(
      base::Value("host-1"), "initialize",
      base::DictValue().Set("protocol", 1).Set("locale", "en\nUS"));
  const std::string line = request.Serialize();
  ASSERT_FALSE(line.empty());
  EXPECT_EQ(line.back(), '\n');
  EXPECT_EQ(line.find('\n'), line.size() - 1);
  auto parsed = ParseJsonRpcLine(line.substr(0, line.size() - 1));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->kind, JsonRpcMessage::Kind::kRequest);
  EXPECT_EQ(*parsed->params.FindString("locale"), "en\nUS");

  const std::string error =
      JsonRpcMessage::Error(base::Value(3),
                            JsonRpcError(kJsonRpcMethodNotFound, "nope"))
          .Serialize();
  EXPECT_EQ(error,
            "{\"error\":{\"code\":-32601,\"message\":\"nope\"},\"id\":3,"
            "\"jsonrpc\":\"2.0\"}\n");
  EXPECT_EQ(JsonRpcMessage::Notification("snapshot", base::DictValue())
                .Serialize(),
            "{\"jsonrpc\":\"2.0\",\"method\":\"snapshot\",\"params\":{}}\n");
}

TEST(JsonRpcTest, OverlongLinesAreDiscardedWhole) {
  JsonRpcLineReader reader(/*max_line_bytes=*/8);
  std::vector<std::string> lines = reader.Append("0123456789abcdef");
  EXPECT_TRUE(lines.empty());
  lines = reader.Append("still the same line\n{\"ok\":1}\n");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "{\"ok\":1}");
  EXPECT_EQ(reader.dropped_overlong(), 1u);
}

}  // namespace
}  // namespace views_shell
