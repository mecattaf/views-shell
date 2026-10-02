// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/ui_tree/binding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace views_shell::ui_tree {
namespace {

bool IsNumber(const base::Value& v) {
  return v.is_int() || v.is_double();
}

// std::round on the value: halves away from zero, exact (no x + 0.5 error).
int64_t RoundHalfAway(double x) {
  const double t = std::trunc(x);
  const double f = x - t;
  return static_cast<int64_t>(t) + (f >= 0.5 ? 1 : (f <= -0.5 ? -1 : 0));
}

int64_t FloorToInt(double x) {
  const double f = std::floor(x);
  if (f >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return std::numeric_limits<int64_t>::max();
  }
  if (f <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
    return std::numeric_limits<int64_t>::min();
  }
  return static_cast<int64_t>(f);
}

// Python's divmod for a positive divisor: the quotient floors.
std::pair<int64_t, int64_t> DivMod(int64_t a, int64_t b) {
  int64_t q = a / b;
  int64_t r = a % b;
  if (r < 0) {
    --q;
    r += b;
  }
  return {q, r};
}

std::string FormatDuration(double v) {
  const int64_t s = FloorToInt(v);
  const auto [d, rem_d] = DivMod(s, 86400);
  const auto [h, rem_h] = DivMod(rem_d, 3600);
  const auto [m, sec] = DivMod(rem_h, 60);
  if (d) {
    return base::StringPrintf("%lld d %lld h", static_cast<long long>(d),
                              static_cast<long long>(h));
  }
  if (h) {
    return base::StringPrintf("%lld h %lld min", static_cast<long long>(h),
                              static_cast<long long>(m));
  }
  if (m) {
    return base::StringPrintf("%lld min", static_cast<long long>(m));
  }
  return base::StringPrintf("%lld s", static_cast<long long>(sec));
}

struct Unit {
  const char* name;
};

std::string FormatBytes(double v) {
  const int64_t n = FloorToInt(v);
  if (n < 1024) {
    return base::StrCat({base::NumberToString(n), " B"});
  }
  static constexpr Unit kUnits[] = {{"KB"}, {"MB"}, {"GB"}, {"TB"}, {"PB"}};
  double value = static_cast<double>(n);
  std::string_view unit;
  for (const Unit& u : kUnits) {
    value /= 1024;
    unit = u.name;
    if (value < 1024) {
      break;
    }
  }
  if (value < 100) {
    const int64_t tenths = RoundHalfAway(value * 10);
    const auto [whole, tenth] = DivMod(tenths, 10);
    return base::StrCat({base::NumberToString(whole), ".",
                         base::NumberToString(tenth), " ", unit});
  }
  return base::StrCat({base::NumberToString(RoundHalfAway(value)), " ", unit});
}

// Fixed-width decimal digits at the front of `s`, advancing it.
bool TakeDigits(std::string_view& s, size_t count, int* out) {
  if (s.size() < count) {
    return false;
  }
  int value = 0;
  for (char c : s.substr(0, count)) {
    if (!base::IsAsciiDigit(c)) {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  s.remove_prefix(count);
  *out = value;
  return true;
}

bool TakeChar(std::string_view& s, char c) {
  if (s.empty() || s.front() != c) {
    return false;
  }
  s.remove_prefix(1);
  return true;
}

// YYYY-MM-DDTHH:MM[:SS[.f{1,9}]](Z|+HH:MM|+HHMM|-HH:MM|-HHMM), the subset
// tools/ui-tree-render.py accepts (ISO_INSTANT there).
std::optional<base::Time> ParseIsoInstant(std::string_view s) {
  base::Time::Exploded e = {};
  int year, month, day, hour, minute, second = 0, micros = 0;
  if (!TakeDigits(s, 4, &year) || !TakeChar(s, '-') ||
      !TakeDigits(s, 2, &month) || !TakeChar(s, '-') ||
      !TakeDigits(s, 2, &day) || !TakeChar(s, 'T') ||
      !TakeDigits(s, 2, &hour) || !TakeChar(s, ':') ||
      !TakeDigits(s, 2, &minute)) {
    return std::nullopt;
  }
  if (TakeChar(s, ':')) {
    if (!TakeDigits(s, 2, &second)) {
      return std::nullopt;
    }
    if (TakeChar(s, '.')) {
      size_t digits = 0;
      while (!s.empty() && base::IsAsciiDigit(s.front()) && digits < 9) {
        if (digits < 6) {
          micros = micros * 10 + (s.front() - '0');
        }
        s.remove_prefix(1);
        ++digits;
      }
      if (digits == 0) {
        return std::nullopt;
      }
      for (size_t i = digits; i < 6; ++i) {
        micros *= 10;
      }
    }
  }
  int offset_minutes = 0;
  if (!TakeChar(s, 'Z')) {
    int sign = 0;
    if (TakeChar(s, '+')) {
      sign = 1;
    } else if (TakeChar(s, '-')) {
      sign = -1;
    } else {
      return std::nullopt;
    }
    int oh, om;
    if (!TakeDigits(s, 2, &oh)) {
      return std::nullopt;
    }
    TakeChar(s, ':');
    if (!TakeDigits(s, 2, &om) || oh > 23 || om > 59) {
      return std::nullopt;
    }
    offset_minutes = sign * (oh * 60 + om);
  }
  if (!s.empty() || month < 1 || month > 12 || day < 1 || hour > 23 ||
      minute > 59 || second > 59) {
    return std::nullopt;
  }
  e.year = year;
  e.month = month;
  e.day_of_month = day;
  e.hour = hour;
  e.minute = minute;
  e.second = second;
  base::Time t;
  if (!base::Time::FromUTCExploded(e, &t)) {
    return std::nullopt;
  }
  return t + base::Microseconds(micros) - base::Minutes(offset_minutes);
}

std::optional<base::Time> AsInstant(const base::Value& v) {
  if (IsNumber(v)) {
    return base::Time::FromSecondsSinceUnixEpoch(v.GetDouble());
  }
  if (v.is_string()) {
    return ParseIsoInstant(v.GetString());
  }
  return std::nullopt;
}

std::string FormatTime(base::Time t, FormatClock::Zone zone) {
  base::Time::Exploded e;
  if (zone == FormatClock::Zone::kUtc) {
    t.UTCExplode(&e);
  } else {
    t.LocalExplode(&e);
  }
  return base::StringPrintf("%02d:%02d", e.hour, e.minute);
}

std::string FormatRelative(base::Time t, base::Time now) {
  const int64_t delta = FloorToInt((now - t).InSecondsF());
  const int64_t span = delta < 0 ? -delta : delta;
  if (span < 60) {
    return "now";
  }
  std::string text;
  if (span < 3600) {
    text = base::StrCat({base::NumberToString(span / 60), " min"});
  } else if (span < 86400) {
    text = base::StrCat({base::NumberToString(span / 3600), " h"});
  } else {
    text = base::StrCat({base::NumberToString(span / 86400), " d"});
  }
  return delta > 0 ? base::StrCat({text, " ago"}) : base::StrCat({"in ", text});
}

// The shortest round-trip digits of a finite, non-zero |v| and the decimal
// exponent of the first one, read from base::NumberToString's EcmaScript form
// ("123.4", "0.0001", "1e-7", "1.5e+21").
void ShortestDigits(double v, std::string* digits, int* exponent) {
  std::string s = base::NumberToString(std::fabs(v));
  int exp = 0;
  const size_t e = s.find('e');
  if (e != std::string::npos) {
    std::string_view tail = std::string_view(s).substr(e + 1);
    if (tail.starts_with("+")) {
      tail.remove_prefix(1);
    }
    CHECK(base::StringToInt(tail, &exp));
    s.resize(e);
  }
  std::string all;
  int point = static_cast<int>(s.size());
  const size_t dot = s.find('.');
  if (dot != std::string::npos) {
    point = static_cast<int>(dot);
    all = base::StrCat({std::string_view(s).substr(0, dot),
                        std::string_view(s).substr(dot + 1)});
  } else {
    all = s;
  }
  size_t lead = 0;
  while (lead < all.size() && all.substr(lead, 1) == "0") {
    ++lead;
  }
  std::string d = all.substr(lead);
  while (d.size() > 1 && d.back() == '0') {
    d.pop_back();
  }
  *digits = d;
  *exponent = point - static_cast<int>(lead) - 1 + exp;
}

void AppendEscaped(std::string_view s, std::string* out) {
  out->push_back('"');
  for (char c : s) {
    switch (c) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      case '\b':
        out->append("\\b");
        break;
      case '\f':
        out->append("\\f");
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out->append(base::StringPrintf("\\u%04x", c));
        } else {
          out->push_back(c);
        }
    }
  }
  out->push_back('"');
}

void AppendJson(const base::Value& v,
                bool pretty,
                int level,
                std::string* out) {
  const std::string indent(pretty ? 2 * (level + 1) : 0, ' ');
  const std::string close_indent(pretty ? 2 * level : 0, ' ');
  switch (v.type()) {
    case base::Value::Type::NONE:
      out->append("null");
      return;
    case base::Value::Type::BOOLEAN:
      out->append(v.GetBool() ? "true" : "false");
      return;
    case base::Value::Type::INTEGER:
      out->append(base::NumberToString(v.GetInt()));
      return;
    case base::Value::Type::DOUBLE:
      out->append(PythonFloatRepr(v.GetDouble()));
      return;
    case base::Value::Type::STRING:
      AppendEscaped(v.GetString(), out);
      return;
    case base::Value::Type::BINARY:
      // Never in a ui tree or a snapshot read from JSON.
      out->append("null");
      return;
    case base::Value::Type::DICT: {
      const base::DictValue& dict = v.GetDict();
      if (dict.empty()) {
        out->append("{}");
        return;
      }
      out->append(pretty ? "{\n" : "{");
      bool first = true;
      for (const auto [key, value] : dict) {
        if (!first) {
          out->append(pretty ? ",\n" : ",");
        }
        first = false;
        out->append(indent);
        AppendEscaped(key, out);
        out->append(pretty ? ": " : ":");
        AppendJson(value, pretty, level + 1, out);
      }
      out->append(pretty ? "\n" : "");
      out->append(close_indent);
      out->push_back('}');
      return;
    }
    case base::Value::Type::LIST: {
      const base::ListValue& list = v.GetList();
      if (list.empty()) {
        out->append("[]");
        return;
      }
      out->append(pretty ? "[\n" : "[");
      bool first = true;
      for (const base::Value& item : list) {
        if (!first) {
          out->append(pretty ? ",\n" : ",");
        }
        first = false;
        out->append(indent);
        AppendJson(item, pretty, level + 1, out);
      }
      out->append(pretty ? "\n" : "");
      out->append(close_indent);
      out->push_back(']');
      return;
    }
  }
}

}  // namespace

std::optional<Format> ParseFormat(std::string_view name) {
  if (name == "text") {
    return Format::kText;
  }
  if (name == "percent") {
    return Format::kPercent;
  }
  if (name == "duration") {
    return Format::kDuration;
  }
  if (name == "bytes") {
    return Format::kBytes;
  }
  if (name == "time") {
    return Format::kTime;
  }
  if (name == "relative-time") {
    return Format::kRelativeTime;
  }
  return std::nullopt;
}

Snapshot::Snapshot() = default;
Snapshot::Snapshot(Snapshot&&) = default;
Snapshot& Snapshot::operator=(Snapshot&&) = default;
Snapshot::~Snapshot() = default;

Snapshot Snapshot::Clone() const {
  Snapshot copy;
  if (state) {
    copy.state = state->Clone();
  }
  copy.sources = sources.Clone();
  return copy;
}

// static
base::expected<Snapshot, std::string> Snapshot::FromJson(
    const base::Value& document) {
  const base::DictValue* dict = document.GetIfDict();
  if (!dict) {
    return base::unexpected("a snapshot file must be an object");
  }
  Snapshot snapshot;
  if (const base::Value* state = dict->Find("snapshot")) {
    snapshot.state = state->Clone();
  }
  if (const base::Value* sources = dict->Find("sources")) {
    if (!sources->is_dict()) {
      return base::unexpected("sources must be an object");
    }
    snapshot.sources = sources->GetDict().Clone();
  }
  return snapshot;
}

// static
FormatClock FormatClock::ForTrace() {
  base::Time::Exploded e = {};
  e.year = 2026;
  e.month = 10;
  e.day_of_month = 2;
  FormatClock clock;
  CHECK(base::Time::FromUTCExploded(e, &clock.now));
  clock.zone = Zone::kUtc;
  return clock;
}

const base::Value* EvaluatePointer(const base::Value& base,
                                   std::string_view pointer) {
  if (pointer.empty()) {
    return &base;
  }
  const base::Value* cur = &base;
  std::vector<std::string_view> raw = base::SplitStringPiece(
      pointer, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  // The text before the first "/" is not a token (RFC 6901).
  raw.erase(raw.begin());
  for (std::string_view r : raw) {
    std::string tok(r);
    base::ReplaceSubstringsAfterOffset(&tok, 0, "~1", "/");
    base::ReplaceSubstringsAfterOffset(&tok, 0, "~0", "~");
    if (const base::DictValue* dict = cur->GetIfDict()) {
      cur = dict->Find(tok);
      if (!cur) {
        return nullptr;
      }
      continue;
    }
    const base::ListValue* list = cur->GetIfList();
    size_t index = 0;
    const bool digits = !tok.empty() && std::ranges::all_of(tok, [](char c) {
      return base::IsAsciiDigit(c);
    });
    if (!list || !digits || (tok.size() > 1 && tok.front() == '0') ||
        !base::StringToSizeT(tok, &index) || index >= list->size()) {
      return nullptr;
    }
    cur = &(*list)[index];
  }
  return cur;
}

std::string PythonFloatRepr(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value > 0 ? "Infinity" : "-Infinity";
  }
  if (value == 0) {
    return std::signbit(value) ? "-0.0" : "0.0";
  }
  std::string digits;
  int exponent = 0;
  ShortestDigits(value, &digits, &exponent);
  std::string out = value < 0 ? "-" : "";
  if (exponent >= -4 && exponent < 16) {
    if (exponent >= 0) {
      const size_t whole = static_cast<size_t>(exponent) + 1;
      std::string int_part = digits.substr(0, std::min(whole, digits.size()));
      int_part.append(whole - int_part.size(), '0');
      const std::string frac =
          digits.size() > whole ? digits.substr(whole) : std::string("0");
      return base::StrCat({out, int_part, ".", frac});
    }
    return base::StrCat({out, "0.",
                         std::string(static_cast<size_t>(-exponent - 1), '0'),
                         digits});
  }
  out.append(digits.substr(0, 1));
  if (digits.size() > 1) {
    out.append(".");
    out.append(digits.substr(1));
  }
  return base::StrCat({out, "e", exponent < 0 ? "-" : "+",
                       base::StringPrintf("%02d", std::abs(exponent))});
}

std::string ToPythonJson(const base::Value& value, bool pretty) {
  std::string out;
  AppendJson(value, pretty, 0, &out);
  return out;
}

std::string AsText(const base::Value& value) {
  switch (value.type()) {
    case base::Value::Type::NONE:
      return "";
    case base::Value::Type::BOOLEAN:
      return value.GetBool() ? "true" : "false";
    case base::Value::Type::INTEGER:
      return base::NumberToString(value.GetInt());
    case base::Value::Type::STRING:
      return value.GetString();
    case base::Value::Type::DOUBLE: {
      const double d = value.GetDouble();
      if (std::isfinite(d) && d == std::trunc(d)) {
        if (std::fabs(d) < 9.0e18) {
          return base::NumberToString(static_cast<int64_t>(d));
        }
        std::string digits;
        int exponent = 0;
        ShortestDigits(d, &digits, &exponent);
        digits.append(static_cast<size_t>(exponent) + 1 - digits.size(), '0');
        return base::StrCat({d < 0 ? "-" : "", digits});
      }
      return PythonFloatRepr(d);
    }
    case base::Value::Type::BINARY:
    case base::Value::Type::DICT:
    case base::Value::Type::LIST:
      return ToPythonJson(value, /*pretty=*/false);
  }
}

base::Value ApplyFormat(Format format,
                        const base::Value& value,
                        const FormatClock& clock) {
  switch (format) {
    case Format::kNone:
      return value.Clone();
    case Format::kPercent:
      if (IsNumber(value)) {
        return base::Value(base::StrCat(
            {base::NumberToString(RoundHalfAway(value.GetDouble() * 100)),
             "%"}));
      }
      break;
    case Format::kDuration:
      if (IsNumber(value)) {
        return base::Value(FormatDuration(value.GetDouble()));
      }
      break;
    case Format::kBytes:
      if (IsNumber(value)) {
        return base::Value(FormatBytes(value.GetDouble()));
      }
      break;
    case Format::kTime:
    case Format::kRelativeTime:
      if (std::optional<base::Time> t = AsInstant(value)) {
        if (format == Format::kTime) {
          return base::Value(FormatTime(*t, clock.zone));
        }
        return base::Value(FormatRelative(
            *t, clock.now.is_null() ? base::Time::Now() : clock.now));
      }
      break;
    case Format::kText:
      break;
  }
  return base::Value(AsText(value));
}

BindingResolver::BindingResolver(const Snapshot* snapshot, FormatClock clock)
    : snapshot_(snapshot), clock_(clock) {}

BindingResolver::~BindingResolver() = default;

base::Value BindingResolver::Resolve(const base::DictValue& binding) const {
  if (!snapshot_) {
    return base::Value(binding.Clone());
  }
  const std::string* bind = binding.FindString("$bind");
  CHECK(bind);
  std::string_view pointer = *bind;
  const base::Value* base = nullptr;
  if (const std::string* source = binding.FindString("source")) {
    base = snapshot_->sources.Find(*source);
    if (pointer.starts_with(".")) {
      pointer.remove_prefix(1);
    }
  } else if (pointer.starts_with(".")) {
    base = item_;
    pointer.remove_prefix(1);
  } else if (snapshot_->state) {
    base = &*snapshot_->state;
  }
  const base::Value* value = base ? EvaluatePointer(*base, pointer) : nullptr;
  if (!value) {
    return base::Value(base::DictValue().Set("$unresolved", *bind));
  }
  Format format = Format::kNone;
  if (const std::string* name = binding.FindString("format")) {
    format = ParseFormat(*name).value_or(Format::kNone);
  }
  return ApplyFormat(format, *value, clock_);
}

base::Value BindingResolver::ResolveValue(const base::Value& value) const {
  if (const base::DictValue* dict = value.GetIfDict()) {
    if (dict->contains("$bind")) {
      return Resolve(*dict);
    }
    base::DictValue out;
    for (const auto [key, item] : *dict) {
      out.Set(key, ResolveValue(item));
    }
    return base::Value(std::move(out));
  }
  if (const base::ListValue* list = value.GetIfList()) {
    base::ListValue out;
    for (const base::Value& item : *list) {
      out.Append(ResolveValue(item));
    }
    return base::Value(std::move(out));
  }
  return value.Clone();
}

}  // namespace views_shell::ui_tree
