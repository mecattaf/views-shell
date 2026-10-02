// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Bindings and formats of a ui tree (schemas/ui-tree-rendering.md, "Bindings"
// and "Formats"), exactly as the reference tools/ui-tree-render.py resolves
// them, so the C++ trace is the Python trace byte for byte:
//
//   {"$bind": "/a/b"}        an RFC 6901 pointer into the snapshot's state
//   {"$bind": "./a"}         inside a repeat, into the current item
//   {"$bind": ..., "source": "<plugin>/<source>"}
//                            into that source's snapshot; the caller supplies
//                            the sources (Snapshot::sources)
//   {"$bind": ..., "format": "percent" | "duration" | "bytes" | "time" |
//                            "relative-time" | "text"}
//
// A pointer that does not resolve gives {"$unresolved": "<the $bind string>"};
// null is a resolved value. time and relative-time read the clock and zone the
// caller passes (FormatClock), so the trace can use a fixed clock and UTC while
// the drawn shell uses the real clock and the local zone.

#ifndef VIEWS_SHELL_UI_TREE_BINDING_H_
#define VIEWS_SHELL_UI_TREE_BINDING_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace views_shell::ui_tree {

enum class Format {
  kNone,
  kText,
  kPercent,
  kDuration,
  kBytes,
  kTime,
  kRelativeTime,
};

std::optional<Format> ParseFormat(std::string_view name);

// What bindings read: the entry's state (a T1 entry's state, or a T2 plugin's
// latest snapshot) and the snapshots of the sources the tree may name.
struct Snapshot {
  Snapshot();
  Snapshot(Snapshot&&);
  Snapshot& operator=(Snapshot&&);
  ~Snapshot();

  Snapshot Clone() const;

  // Absent: every absolute pointer is unresolved.
  std::optional<base::Value> state;
  // "<plugin>/<source>" -> that source's snapshot.
  base::DictValue sources;

  // The fixture shape, {"snapshot": <state>, "sources": {...}}.
  static base::expected<Snapshot, std::string> FromJson(
      const base::Value& document);
};

struct FormatClock {
  enum class Zone { kLocal, kUtc };

  // The instant relative-time measures against; null means base::Time::Now()
  // at each call.
  base::Time now;
  Zone zone = Zone::kLocal;

  // The trace's clock: 2026-10-02T00:00:00Z, UTC.
  static FormatClock ForTrace();
};

// RFC 6901 evaluation of `pointer` ("" is the whole value) against `base`.
// Null when a token does not resolve. An array index is decimal without
// leading zeros.
const base::Value* EvaluatePointer(const base::Value& base,
                                   std::string_view pointer);

// The format table of schemas/ui-tree-rendering.md. kNone returns the value
// unchanged; a value of the wrong kind for its format is rendered as kText.
base::Value ApplyFormat(Format format,
                        const base::Value& value,
                        const FormatClock& clock);

// `text`: a string as is, true/false, an integral number without ".0",
// another double in Python's shortest round-trip form, null as "", arrays and
// objects as compact JSON with sorted keys.
std::string AsText(const base::Value& value);

// Python's repr() of a float (what json.dumps writes): the shortest digits
// that round-trip, in fixed notation when the decimal exponent is in [-4, 16)
// and as d.ddde±XX otherwise; an integral value keeps ".0".
std::string PythonFloatRepr(double value);

// JSON as Python's json.dumps(sort_keys=True, ensure_ascii=False) writes it:
// `indent` 2 with ": " and "," line breaks (the trace), or compact with ","
// and ":" (AsText). base::DictValue is already sorted by key.
std::string ToPythonJson(const base::Value& value, bool pretty);

// Resolves the values of one render pass. Without a snapshot (has_snapshot()
// false) bindings stay as written, as the reference tool does without
// --snapshot.
class BindingResolver {
 public:
  BindingResolver(const Snapshot* snapshot, FormatClock clock);
  BindingResolver(const BindingResolver&) = delete;
  BindingResolver& operator=(const BindingResolver&) = delete;
  ~BindingResolver();

  bool has_snapshot() const { return snapshot_ != nullptr; }
  const FormatClock& clock() const { return clock_; }

  // The current repeat item (`.`), or null outside a repeat.
  const base::Value* item() const { return item_; }
  void set_item(const base::Value* item) { item_ = item; }

  // One binding object.
  base::Value Resolve(const base::DictValue& binding) const;

  // A property value: bindings resolved, objects and arrays walked.
  base::Value ResolveValue(const base::Value& value) const;

 private:
  raw_ptr<const Snapshot> snapshot_;
  FormatClock clock_;
  raw_ptr<const base::Value> item_ = nullptr;
};

}  // namespace views_shell::ui_tree

#endif  // VIEWS_SHELL_UI_TREE_BINDING_H_
