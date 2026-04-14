// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/mmr_reranker.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace rimeclaw {

namespace {

// Tokenize a string into a set of lowercase words
std::unordered_set<std::string> tokenize(const std::string& text) {
  std::unordered_set<std::string> tokens;
  std::istringstream ss(text);
  std::string word;
  while (ss >> word) {
    // Lowercase
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // Strip leading/trailing punctuation
    while (!word.empty() && std::ispunct(static_cast<unsigned char>(word.front())))
      word.erase(word.begin());
    while (!word.empty() && std::ispunct(static_cast<unsigned char>(word.back())))
      word.pop_back();
    if (!word.empty())
      tokens.insert(word);
  }
  return tokens;
}

// Jaccard similarity between two token sets
double jaccard_similarity(const std::unordered_set<std::string>& a,
                          const std::unordered_set<std::string>& b) {
  if (a.empty() && b.empty())
    return 1.0;
  if (a.empty() || b.empty())
    return 0.0;

  int intersection = 0;
  for (const auto& t : a) {
    if (b.count(t))
      ++intersection;
  }
  int union_size = static_cast<int>(a.size() + b.size()) - intersection;
  return union_size > 0 ? static_cast<double>(intersection) / union_size : 0.0;
}

}  // namespace

std::vector<RankedItem> MMRReranker::Rerank(const std::vector<RankedItem>& items,
                                            int top_k, double lambda) {
  if (items.empty())
    return {};

  int k = (top_k <= 0 || top_k > static_cast<int>(items.size()))
              ? static_cast<int>(items.size())
              : top_k;

  // Precompute token sets
  std::vector<std::unordered_set<std::string>> token_sets;
  token_sets.reserve(items.size());
  for (const auto& item : items) {
    token_sets.push_back(tokenize(item.content));
  }

  std::vector<bool> picked(items.size(), false);
  std::vector<RankedItem> selected;
  selected.reserve(k);

  for (int round = 0; round < k; ++round) {
    int best_idx = -1;
    double best_mmr = -std::numeric_limits<double>::infinity();

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      if (picked[i])
        continue;

      // Max similarity to already-selected items
      double max_sim = 0.0;
      for (const auto& sel : selected) {
        // Find sel's index to get its token set
        int sel_idx = -1;
        for (int j = 0; j < static_cast<int>(items.size()); ++j) {
          if (items[j].id == sel.id) {
            sel_idx = j;
            break;
          }
        }
        double sim = (sel_idx >= 0)
                         ? jaccard_similarity(token_sets[i], token_sets[sel_idx])
                         : 0.0;
        if (sim > max_sim)
          max_sim = sim;
      }

      // MMR score
      double mmr = lambda * items[i].score - (1.0 - lambda) * max_sim;

      if (mmr > best_mmr) {
        best_mmr = mmr;
        best_idx = i;
      }
    }

    if (best_idx >= 0) {
      picked[best_idx] = true;
      selected.push_back(items[best_idx]);
    }
  }

  return selected;
}

}  // namespace rimeclaw
