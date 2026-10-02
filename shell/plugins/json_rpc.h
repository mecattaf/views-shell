// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// JSON-RPC 2.0 as the T2 plugin protocol frames it (schemas/plugin-protocol.md):
// one JSON object per line on a pipe, `\n`-terminated, empty lines ignored. A
// line that is not JSON, or is JSON but not a JSON-RPC 2.0 object, is dropped
// by the receiver and never answered (no -32700: a reply with id null names no
// request on a line-framed pipe).
//
// Nothing here does IO. JsonRpcLineReader turns arbitrary byte chunks (a pipe
// read can end anywhere, inside a line or inside a UTF-8 sequence) into whole
// lines; ParseJsonRpcLine() classifies one line; JsonRpcMessage::Serialize()
// writes one.

#ifndef VIEWS_SHELL_PLUGINS_JSON_RPC_H_
#define VIEWS_SHELL_PLUGINS_JSON_RPC_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "base/values.h"

namespace views_shell {

// The protocol version views-shell speaks (`engines.protocol`).
inline constexpr int kPluginProtocolVersion = 1;

// Error codes. The first three are JSON-RPC 2.0's own; -32001 is the
// protocol's "permission not declared"; -32000 is views-shell's code for a
// request the host could not complete (no answer in time, the plugin exited,
// the compositor refused a command).
inline constexpr int kJsonRpcMethodNotFound = -32601;
inline constexpr int kJsonRpcInvalidParams = -32602;
inline constexpr int kJsonRpcInternalError = -32603;
inline constexpr int kJsonRpcPermissionNotDeclared = -32001;
inline constexpr int kJsonRpcRequestFailed = -32000;

struct JsonRpcError {
  JsonRpcError();
  JsonRpcError(int code, std::string message);
  JsonRpcError(int code, std::string message, base::Value data);
  JsonRpcError(const JsonRpcError&);
  JsonRpcError(JsonRpcError&&);
  JsonRpcError& operator=(const JsonRpcError&);
  JsonRpcError& operator=(JsonRpcError&&);
  ~JsonRpcError();

  // {code, message[, data]}.
  base::DictValue ToDict() const;

  int code = kJsonRpcInternalError;
  std::string message;
  base::Value data;  // NONE when absent
};

// The answer to a request: a result value, or an error.
using JsonRpcResult = base::expected<base::Value, JsonRpcError>;

struct JsonRpcMessage {
  enum class Kind {
    kRequest,       // method and id
    kNotification,  // method, no id
    kResult,        // id and result
    kError,         // id and error
  };

  JsonRpcMessage();
  JsonRpcMessage(const JsonRpcMessage&) = delete;
  JsonRpcMessage(JsonRpcMessage&&);
  JsonRpcMessage& operator=(const JsonRpcMessage&) = delete;
  JsonRpcMessage& operator=(JsonRpcMessage&&);
  ~JsonRpcMessage();

  static JsonRpcMessage Request(base::Value id,
                                std::string method,
                                base::DictValue params);
  static JsonRpcMessage Notification(std::string method,
                                     base::DictValue params);
  static JsonRpcMessage Result(base::Value id, base::Value result);
  static JsonRpcMessage Error(base::Value id, JsonRpcError error);

  // One line: compact JSON and a final "\n".
  std::string Serialize() const;

  bool is_answer() const {
    return kind == Kind::kResult || kind == Kind::kError;
  }

  Kind kind = Kind::kNotification;
  base::Value id;          // string or integer; NONE for a notification
  std::string method;      // requests and notifications
  base::DictValue params;  // requests and notifications; {} when absent
  base::Value result;      // kResult
  JsonRpcError error;      // kError
};

// Classifies one line (without its "\n"). The error string says why the line
// is dropped: not JSON, not an object, no "jsonrpc": "2.0", or a shape that is
// neither a request, a notification nor an answer. Params that are present
// must be an object: every protocol method takes named params.
base::expected<JsonRpcMessage, std::string> ParseJsonRpcLine(
    std::string_view line);

// A short printable form of an id, for logs: "host-3", 7, null.
std::string JsonRpcIdToString(const base::Value& id);

// Reassembles lines from a byte stream. A line longer than max_line_bytes is
// discarded whole (up to its terminating "\n") and counted in
// dropped_overlong(); a stream cannot make the reader buffer without bound.
class JsonRpcLineReader {
 public:
  static constexpr size_t kDefaultMaxLineBytes = 16 * 1024 * 1024;

  explicit JsonRpcLineReader(size_t max_line_bytes = kDefaultMaxLineBytes);
  JsonRpcLineReader(const JsonRpcLineReader&) = delete;
  JsonRpcLineReader& operator=(const JsonRpcLineReader&) = delete;
  ~JsonRpcLineReader();

  // Feeds a chunk; returns every line it completed, in order, without the
  // "\n" (and without a "\r" before it). Lines that are empty or only
  // whitespace are skipped.
  std::vector<std::string> Append(std::string_view bytes);

  // End of stream: the unterminated rest, if it is not blank. The protocol
  // terminates every line, so a non-empty rest is logged by callers.
  std::string TakeRemainder();

  size_t dropped_overlong() const { return dropped_overlong_; }

 private:
  const size_t max_line_bytes_;
  std::string pending_;
  bool discarding_ = false;
  size_t dropped_overlong_ = 0;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_JSON_RPC_H_
