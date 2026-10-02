// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/registry_view.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {

namespace {

base::Value OrNull(const base::DictValue& dict, std::string_view key) {
  const base::Value* value = dict.Find(key);
  return value ? value->Clone() : base::Value();
}

const base::ListValue& ListOrEmpty(const base::DictValue* dict,
                                   std::string_view key) {
  static const base::NoDestructor<base::ListValue> empty;
  const base::ListValue* list = dict ? dict->FindList(key) : nullptr;
  return list ? *list : *empty;
}

base::ListValue SortedStrings(const base::ListValue& list) {
  std::vector<std::string> all;
  for (const base::Value& item : list) {
    all.push_back(item.GetString());
  }
  std::sort(all.begin(), all.end());
  base::ListValue out;
  for (std::string& item : all) {
    out.Append(std::move(item));
  }
  return out;
}

// Python's json encoder with ensure_ascii=False: only ", \ and control
// characters are escaped.
void AppendJsonString(std::string_view text, std::string* out) {
  out->push_back('"');
  for (char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      case '\b':
        *out += "\\b";
        break;
      case '\f':
        *out += "\\f";
        break;
      default:
        if (c < 0x20) {
          *out += base::StringPrintf("\\u%04x", c);
        } else {
          out->push_back(ch);
        }
    }
  }
  out->push_back('"');
}

// float.__repr__ for the values a manifest carries.
std::string PythonFloat(double value) {
  if (std::isfinite(value) && value == std::floor(value) &&
      std::fabs(value) < 1e16) {
    return base::StringPrintf("%.1f", value);
  }
  return base::NumberToString(value);
}

void Write(const base::Value& value, int depth, std::string* out) {
  const std::string indent(2 * (depth + 1), ' ');
  const std::string close_indent(2 * depth, ' ');
  switch (value.type()) {
    case base::Value::Type::NONE:
      *out += "null";
      return;
    case base::Value::Type::BOOLEAN:
      *out += value.GetBool() ? "true" : "false";
      return;
    case base::Value::Type::INTEGER:
      *out += base::NumberToString(value.GetInt());
      return;
    case base::Value::Type::DOUBLE:
      *out += PythonFloat(value.GetDouble());
      return;
    case base::Value::Type::STRING:
      AppendJsonString(value.GetString(), out);
      return;
    case base::Value::Type::BINARY:
      *out += "null";
      return;
    case base::Value::Type::LIST: {
      const base::ListValue& list = value.GetList();
      if (list.empty()) {
        *out += "[]";
        return;
      }
      *out += "[\n";
      bool first = true;
      for (const base::Value& item : list) {
        if (!first) {
          *out += ",\n";
        }
        first = false;
        *out += indent;
        Write(item, depth + 1, out);
      }
      *out += "\n" + close_indent + "]";
      return;
    }
    case base::Value::Type::DICT: {
      const base::DictValue& dict = value.GetDict();
      if (dict.empty()) {
        *out += "{}";
        return;
      }
      *out += "{\n";
      bool first = true;
      // DictValue iterates in byte order, which is Python's code point order
      // for UTF-8 keys.
      for (const auto [key, item] : dict) {
        if (!first) {
          *out += ",\n";
        }
        first = false;
        *out += indent;
        AppendJsonString(key, out);
        *out += ": ";
        Write(item, depth + 1, out);
      }
      *out += "\n" + close_indent + "}";
      return;
    }
  }
}

}  // namespace

std::string WriteRegistryJson(const base::Value& value) {
  std::string out;
  Write(value, 0, &out);
  out.push_back('\n');
  return out;
}

base::DictValue BuildRegistryView(const PluginManifest& manifest) {
  const base::DictValue& m = manifest.raw;
  const std::string& pid = manifest.id;
  const std::string mode = std::string(PluginTierName(manifest.tier));
  const base::DictValue* c = m.FindDict("contributes");
  const base::DictValue* conf = c ? c->FindDict("configuration") : nullptr;
  const base::DictValue* props = conf ? conf->FindDict("properties") : nullptr;
  auto q = [&manifest](const std::string& ref) {
    return manifest.Qualify(ref);
  };

  base::ListValue commands;
  for (const base::Value& item : ListOrEmpty(c, "commands")) {
    const base::DictValue& cmd = item.GetDict();
    base::ListValue args;
    for (const base::Value& a : ListOrEmpty(&cmd, "args")) {
      args.Append(base::DictValue()
                      .Set("name", *a.GetDict().FindString("name"))
                      .Set("type", *a.GetDict().FindString("type"))
                      .Set("required",
                           a.GetDict().FindBool("required").value_or(false)));
    }
    base::DictValue entry;
    entry.Set("id", q(*cmd.FindString("id")));
    entry.Set("title", *cmd.FindString("title"));
    entry.Set("icon", OrNull(cmd, "icon"));
    entry.Set("category", OrNull(cmd, "category"));
    entry.Set("enablement", OrNull(cmd, "enablement"));
    entry.Set("args", std::move(args));
    entry.Set("confirm", cmd.FindBool("confirm").value_or(false));
    const std::string* result = cmd.FindString("result");
    entry.Set("result", result ? *result : std::string("none"));
    entry.Set("handler", mode == "declarative" ? OrNull(cmd, "handler")
                                               : base::Value());
    entry.Set("surface", OrNull(cmd, "surface"));
    entry.Set("verb", OrNull(cmd, "verb"));
    commands.Append(std::move(entry));
  }

  base::ListValue surfaces;
  std::set<std::string> activation;
  for (const base::Value& item : ListOrEmpty(c, "surfaces")) {
    const base::DictValue& s = item.GetDict();
    const std::string& id = *s.FindString("id");
    const std::string& kind = *s.FindString("kind");
    const std::string* keyboard = s.FindString("keyboard");
    const std::string* section = s.FindString("defaultSection");
    base::DictValue entry;
    entry.Set("id", id);
    entry.Set("kind", kind);
    entry.Set("keyboard", keyboard ? *keyboard : std::string("on-demand"));
    entry.Set("anchor", OrNull(s, "anchor"));
    entry.Set("defaultSection", section ? *section : std::string("center"));
    entry.Set("allowMultiple", s.FindBool("allowMultiple").value_or(false));
    entry.Set("ui", OrNull(s, "ui"));
    entry.Set("prefix", OrNull(s, "prefix"));
    entry.Set("when", OrNull(s, "when"));
    surfaces.Append(std::move(entry));
    if (kind == "bar" || kind == "bar-widget" || kind == "service") {
      activation.insert("onStartup");
    } else if (kind == "picker-provider") {
      activation.insert(base::StrCat({"onPicker:", id}));
    } else {
      activation.insert(base::StrCat({"onSurface:", id}));
    }
  }

  base::ListValue keybindings;
  for (const base::Value& item : ListOrEmpty(c, "keybindings")) {
    const base::DictValue& kb = item.GetDict();
    base::Value key;
    if (const base::Value* literal = kb.Find("key")) {
      key = literal->Clone();
    } else {
      const std::string& from = *kb.FindString("keyFrom");
      const std::string name = from.substr(from.find('.') + 1);
      const base::DictValue* prop = props ? props->FindDict(name) : nullptr;
      if (prop) {
        key = OrNull(*prop, "default");
      }
    }
    // Python adds onKeybinding:<key> for a truthy key: a non-empty string.
    if (const std::string* text = key.GetIfString(); text && !text->empty()) {
      activation.insert(base::StrCat({"onKeybinding:", *text}));
    }
    base::DictValue entry;
    entry.Set("command", q(*kb.FindString("command")));
    entry.Set("key", std::move(key));
    entry.Set("keyFrom", OrNull(kb, "keyFrom"));
    entry.Set("args", OrNull(kb, "args"));
    entry.Set("when", OrNull(kb, "when"));
    entry.Set("release", kb.FindBool("release").value_or(false));
    entry.Set("locked", kb.FindBool("locked").value_or(false));
    entry.Set("mode", OrNull(kb, "mode"));
    keybindings.Append(std::move(entry));
  }

  base::Value cli;
  if (const base::DictValue* raw_cli = c ? c->FindDict("cli") : nullptr) {
    base::ListValue verbs;
    for (const base::Value& item : ListOrEmpty(raw_cli, "verbs")) {
      const base::DictValue& v = item.GetDict();
      verbs.Append(base::DictValue()
                       .Set("verb", *v.FindString("verb"))
                       .Set("command", q(*v.FindString("command")))
                       .Set("summary", OrNull(v, "summary"))
                       .Set("confirm", v.FindBool("confirm").value_or(false)));
    }
    cli = base::Value(base::DictValue()
                          .Set("name", *raw_cli->FindString("name"))
                          .Set("verbs", std::move(verbs)));
  }

  base::ListValue quick;
  for (const base::Value& item : ListOrEmpty(c, "quickSettings")) {
    const base::DictValue& e = item.GetDict();
    const std::string& slot = *e.FindString("slot");
    base::Value size;
    if (slot == "tile") {
      const std::string* raw_size = e.FindString("size");
      size = base::Value(raw_size ? *raw_size : std::string("primary"));
    }
    activation.insert(base::StrCat({"onQuickSettings:", *e.FindString("id")}));
    base::DictValue entry;
    entry.Set("id", *e.FindString("id"));
    entry.Set("slot", slot);
    entry.Set("ui", OrNull(e, "ui"));
    entry.Set("state", OrNull(e, "state"));
    entry.Set("size", std::move(size));
    entry.Set("page", OrNull(e, "page"));
    entry.Set("title", OrNull(e, "title"));
    entry.Set("order", e.FindInt("order").value_or(0));
    entry.Set("when", OrNull(e, "when"));
    quick.Append(std::move(entry));
  }

  base::ListValue sources;
  for (const base::Value& item : ListOrEmpty(c, "sources")) {
    const std::string qualified =
        base::StrCat({pid, "/", *item.GetDict().FindString("id")});
    activation.insert(base::StrCat({"onSource:", qualified}));
    sources.Append(base::DictValue()
                       .Set("id", qualified)
                       .Set("schema", OrNull(item.GetDict(), "schema")));
  }

  for (const base::Value& command : commands) {
    activation.insert(
        base::StrCat({"onCommand:", *command.GetDict().FindString("id")}));
  }
  base::ListValue events;
  for (const base::Value& event : ListOrEmpty(c, "events")) {
    activation.insert(base::StrCat({"onEvent:", event.GetString()}));
    events.Append(event.Clone());
  }
  base::ListValue activation_list;
  for (const std::string& event : activation) {
    activation_list.Append(event);
  }

  const base::DictValue* reqs = m.FindDict("requires");
  const base::DictValue* comp =
      reqs ? reqs->FindDict("compositor") : nullptr;

  base::ListValue menus;
  for (const base::Value& item : ListOrEmpty(c, "menus")) {
    const base::DictValue& x = item.GetDict();
    menus.Append(base::DictValue()
                     .Set("location", *x.FindString("location"))
                     .Set("command", q(*x.FindString("command")))
                     .Set("group", OrNull(x, "group"))
                     .Set("when", OrNull(x, "when")));
  }
  base::ListValue launcher;
  for (const base::Value& item : ListOrEmpty(c, "launcher")) {
    const base::DictValue& x = item.GetDict();
    const base::ListValue* keywords = x.FindList("keywords");
    launcher.Append(
        base::DictValue()
            .Set("command", q(*x.FindString("command")))
            .Set("title", *x.FindString("title"))
            .Set("keywords", keywords ? keywords->Clone() : base::ListValue())
            .Set("icon", OrNull(x, "icon"))
            .Set("desktopEntry", x.FindBool("desktopEntry").value_or(false)));
  }

  base::DictValue properties;
  if (props) {
    for (const auto [name, p] : *props) {
      base::DictValue prop = p.GetDict().Clone();
      if (!prop.contains("scope")) {
        prop.Set("scope", "global");
      }
      if (!prop.contains("default")) {
        prop.Set("default", base::Value());
      }
      properties.Set(name, std::move(prop));
    }
  }

  base::DictValue view;
  view.Set("id", pid);
  view.Set("tier", mode);
  view.Set("activation", std::move(activation_list));
  view.Set("commands", std::move(commands));
  view.Set("surfaces", std::move(surfaces));
  view.Set("capabilities",
           base::DictValue()
               .Set("required", SortedStrings(ListOrEmpty(comp, "required")))
               .Set("optional", SortedStrings(ListOrEmpty(comp, "optional"))));
  view.Set("permissions", SortedStrings(ListOrEmpty(&m, "permissions")));
  view.Set("dependencies", SortedStrings(ListOrEmpty(reqs, "plugins")));
  view.Set("keybindings", std::move(keybindings));
  view.Set("menus", std::move(menus));
  view.Set("launcher", std::move(launcher));
  view.Set("cli", std::move(cli));
  view.Set("events", std::move(events));
  view.Set("sources", std::move(sources));
  view.Set("quickSettings", std::move(quick));
  view.Set("configuration",
           base::DictValue()
               .Set("title", conf ? OrNull(*conf, "title") : base::Value())
               .Set("properties", std::move(properties)));
  return view;
}

}  // namespace views_shell
