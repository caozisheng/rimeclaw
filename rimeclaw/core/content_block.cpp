// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/content_block.hpp"

namespace rimeclaw {

// --- ContentBlock ---

nlohmann::json ContentBlock::ToJson() const {
  nlohmann::json j;
  j["type"] = type;

  if (type == "text" || type == "thinking") {
    j["text"] = text;
  } else if (type == "tool_use") {
    j["id"] = id;
    j["name"] = name;
    j["input"] = input.is_null() ? nlohmann::json::object() : input;
  } else if (type == "tool_result") {
    j["tool_use_id"] = tool_use_id;
    j["content"] = content;
  } else {
    // Generic fallback: include all non-empty fields
    if (!text.empty())
      j["text"] = text;
    if (!id.empty())
      j["id"] = id;
    if (!name.empty())
      j["name"] = name;
    if (!input.is_null())
      j["input"] = input;
    if (!tool_use_id.empty())
      j["tool_use_id"] = tool_use_id;
    if (!content.empty())
      j["content"] = content;
  }

  return j;
}

ContentBlock ContentBlock::FromJson(const nlohmann::json& j) {
  ContentBlock block;
  block.type = j.value("type", "text");

  if (block.type == "text" || block.type == "thinking") {
    block.text = j.value("text", "");
  } else if (block.type == "tool_use") {
    block.id = j.value("id", "");
    block.name = j.value("name", "");
    block.input = j.contains("input") ? j["input"] : nlohmann::json::object();
  } else if (block.type == "tool_result") {
    block.tool_use_id = j.value("tool_use_id", "");
    // content can be string or array
    if (j.contains("content")) {
      if (j["content"].is_string()) {
        block.content = j["content"].get<std::string>();
      } else if (j["content"].is_array()) {
        // Join array of text blocks
        std::string joined;
        for (const auto& item : j["content"]) {
          if (item.is_object() && item.value("type", "") == "text") {
            if (!joined.empty())
              joined += "\n";
            joined += item.value("text", "");
          }
        }
        block.content = joined;
      }
    }
  } else {
    // Generic
    block.text = j.value("text", "");
    block.id = j.value("id", "");
    block.name = j.value("name", "");
    if (j.contains("input"))
      block.input = j["input"];
    block.tool_use_id = j.value("tool_use_id", "");
    if (j.contains("content") && j["content"].is_string())
      block.content = j["content"].get<std::string>();
  }

  return block;
}

ContentBlock ContentBlock::MakeText(const std::string& text) {
  ContentBlock block;
  block.type = "text";
  block.text = text;
  return block;
}

ContentBlock ContentBlock::MakeToolUse(const std::string& id,
                                       const std::string& name,
                                       const nlohmann::json& input) {
  ContentBlock block;
  block.type = "tool_use";
  block.id = id;
  block.name = name;
  block.input = input;
  return block;
}

ContentBlock ContentBlock::MakeToolResult(const std::string& tool_use_id,
                                          const std::string& content) {
  ContentBlock block;
  block.type = "tool_result";
  block.tool_use_id = tool_use_id;
  block.content = content;
  return block;
}


}  // namespace rimeclaw
