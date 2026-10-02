// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/json_rpc.h"

#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace views_shell {

JsonRpcError::JsonRpcError() = default;
JsonRpcError::JsonRpcError(int code, std::string message)
    : code(code), message(std::move(message)) {}
JsonRpcError::JsonRpcError(int code, std::string message, base::Value data)
    : code(code), message(std::move(message)), data(std::move(data)) {}
JsonRpcError::JsonRpcError(const JsonRpcError& other)
    : code(other.code), message(other.message), data(other.data.Clone()) {}
JsonRpcError::JsonRpcError(JsonRpcError&&) = default;
JsonRpcError& JsonRpcError::operator=(const JsonRpcError& other) {
  code = other.code;
  message = other.message;
  data = other.data.Clone();
  return *this;
}
JsonRpcError& JsonRpcError::operator=(JsonRpcError&&) = default;
JsonRpcError::~JsonRpcError() = default;

base::DictValue JsonRpcError::ToDict() const {
  base::DictValue dict;
  dict.Set("code", code);
  dict.Set("message", message);
  if (!data.is_none()) {
    dict.Set("data", data.Clone());
  }
  return dict;
}

JsonRpcMessage::JsonRpcMessage() = default;
JsonRpcMessage::JsonRpcMessage(JsonRpcMessage&&) = default;
JsonRpcMessage& JsonRpcMessage::operator=(JsonRpcMessage&&) = default;
JsonRpcMessage::~JsonRpcMessage() = default;

// static
JsonRpcMessage JsonRpcMessage::Request(base::Value id,
                                       std::string method,
                                       base::DictValue params) {
  JsonRpcMessage message;
  message.kind = Kind::kRequest;
  message.id = std::move(id);
  message.method = std::move(method);
  message.params = std::move(params);
  return message;
}

// static
JsonRpcMessage JsonRpcMessage::Notification(std::string method,
                                            base::DictValue params) {
  JsonRpcMessage message;
  message.kind = Kind::kNotification;
  message.method = std::move(method);
  message.params = std::move(params);
  return message;
}

// static
JsonRpcMessage JsonRpcMessage::Result(base::Value id, base::Value result) {
  JsonRpcMessage message;
  message.kind = Kind::kResult;
  message.id = std::move(id);
  message.result = std::move(result);
  return message;
}

// static
JsonRpcMessage JsonRpcMessage::Error(base::Value id, JsonRpcError error) {
  JsonRpcMessage message;
  message.kind = Kind::kError;
  message.id = std::move(id);
  message.error = std::move(error);
  return message;
}

std::string JsonRpcMessage::Serialize() const {
  base::DictValue dict;
  dict.Set("jsonrpc", "2.0");
  switch (kind) {
    case Kind::kRequest:
      dict.Set("id", id.Clone());
      dict.Set("method", method);
      dict.Set("params", params.Clone());
      break;
    case Kind::kNotification:
      dict.Set("method", method);
      dict.Set("params", params.Clone());
      break;
    case Kind::kResult:
      dict.Set("id", id.Clone());
      dict.Set("result", result.Clone());
      break;
    case Kind::kError:
      dict.Set("id", id.Clone());
      dict.Set("error", error.ToDict());
      break;
  }
  std::string line;
  // Compact output never contains a raw newline: JSONWriter escapes control
  // characters inside strings.
  base::JSONWriter::Write(dict, &line);
  line.push_back('\n');
  return line;
}

base::expected<JsonRpcMessage, std::string> ParseJsonRpcLine(
    std::string_view line) {
  std::optional<base::Value> value =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!value) {
    return base::unexpected("not JSON");
  }
  base::DictValue* dict = value->GetIfDict();
  if (!dict) {
    return base::unexpected("JSON but not an object");
  }
  const std::string* version = dict->FindString("jsonrpc");
  if (!version || *version != "2.0") {
    return base::unexpected("not a JSON-RPC 2.0 object (no \"jsonrpc\": \"2.0\")");
  }
  JsonRpcMessage message;
  base::Value* id = dict->Find("id");
  if (id && !id->is_none() && !id->is_string() && !id->is_int() &&
      !id->is_double()) {
    return base::unexpected("id is neither a string nor a number");
  }
  if (base::Value* method = dict->Find("method")) {
    if (!method->is_string()) {
      return base::unexpected("method is not a string");
    }
    base::Value* params = dict->Find("params");
    if (params && !params->is_dict()) {
      return base::unexpected("params is present and not an object");
    }
    message.kind = id ? JsonRpcMessage::Kind::kRequest
                      : JsonRpcMessage::Kind::kNotification;
    message.method = std::move(method->GetString());
    if (id) {
      message.id = std::move(*id);
    }
    if (params) {
      message.params = std::move(params->GetDict());
    }
    return message;
  }
  if (!id) {
    return base::unexpected("neither a method nor an id");
  }
  base::Value* result = dict->Find("result");
  base::Value* error = dict->Find("error");
  if ((result != nullptr) == (error != nullptr)) {
    return base::unexpected("an answer carries neither or both of result and error");
  }
  message.id = std::move(*id);
  if (result) {
    message.kind = JsonRpcMessage::Kind::kResult;
    message.result = std::move(*result);
    return message;
  }
  const base::DictValue* error_dict = error->GetIfDict();
  std::optional<int> code =
      error_dict ? error_dict->FindInt("code") : std::nullopt;
  const std::string* text =
      error_dict ? error_dict->FindString("message") : nullptr;
  if (!code || !text) {
    return base::unexpected("error is not {code: integer, message: string}");
  }
  message.kind = JsonRpcMessage::Kind::kError;
  message.error.code = *code;
  message.error.message = *text;
  if (const base::Value* data = error_dict->Find("data")) {
    message.error.data = data->Clone();
  }
  return message;
}

std::string JsonRpcIdToString(const base::Value& id) {
  if (const std::string* text = id.GetIfString()) {
    return *text;
  }
  if (id.is_int()) {
    return base::NumberToString(id.GetInt());
  }
  if (id.is_double()) {
    return base::NumberToString(id.GetDouble());
  }
  return "null";
}

JsonRpcLineReader::JsonRpcLineReader(size_t max_line_bytes)
    : max_line_bytes_(max_line_bytes) {}

JsonRpcLineReader::~JsonRpcLineReader() = default;

std::vector<std::string> JsonRpcLineReader::Append(std::string_view bytes) {
  std::vector<std::string> lines;
  while (!bytes.empty()) {
    const size_t newline = bytes.find('\n');
    const std::string_view chunk =
        newline == std::string_view::npos ? bytes : bytes.substr(0, newline);
    if (!discarding_) {
      pending_.append(chunk);
      if (pending_.size() > max_line_bytes_) {
        pending_.clear();
        discarding_ = true;
        ++dropped_overlong_;
      }
    }
    if (newline == std::string_view::npos) {
      break;
    }
    bytes = bytes.substr(newline + 1);
    if (discarding_) {
      discarding_ = false;
      continue;
    }
    std::string line = std::move(pending_);
    pending_.clear();
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!base::TrimWhitespaceASCII(line, base::TRIM_ALL).empty()) {
      lines.push_back(std::move(line));
    }
  }
  return lines;
}

std::string JsonRpcLineReader::TakeRemainder() {
  std::string rest = std::move(pending_);
  pending_.clear();
  discarding_ = false;
  if (base::TrimWhitespaceASCII(rest, base::TRIM_ALL).empty()) {
    return std::string();
  }
  return rest;
}

}  // namespace views_shell
