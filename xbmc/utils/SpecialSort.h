/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <map>
#include <set>
#include <vector>

class SpecialSort
{
public:
  template<typename T>
  static std::vector<T> SortTopologically(const std::vector<std::pair<T, T>>& topology);

private:
  static void sortTopologically(int current,
                                const std::vector<std::set<int>>& edges,
                                std::vector<bool>& visited,
                                std::vector<int>& stack);
};

template<typename T>
std::vector<T> SpecialSort::SortTopologically(const std::vector<std::pair<T, T>>& topology)
{
  typename std::map<int, T> key2ValueMapping;
  typename std::map<T, int> value2KeyMapping;
  std::vector<std::set<int>> edges;

  int key = 0;
  // map the values to comparable and usable keys and build a map with all
  // the sources and their destinations
  for (const auto& topologyIt : topology)
  {
    size_t sourceKey = -1, destKey = -1;
    // handle the source item
    typename std::map<T, int>::const_iterator it = value2KeyMapping.find(topologyIt.first);
    if (it == value2KeyMapping.end())
    {
      sourceKey = key;
      value2KeyMapping.insert(make_pair(topologyIt.first, sourceKey));
      key2ValueMapping.insert(make_pair(sourceKey, topologyIt.first));
      key++;
    }
    else
      sourceKey = it->second;

    // handle the destination item
    it = value2KeyMapping.find(topologyIt.second);
    if (it == value2KeyMapping.end())
    {
      destKey = key;
      value2KeyMapping.insert(make_pair(topologyIt.second, destKey));
      key2ValueMapping.insert(make_pair(destKey, topologyIt.second));
      key++;
    }
    else
      destKey = it->second;

    // add the combination to the edges mapping
    if (edges.size() <= sourceKey)
      edges.insert(edges.end(), sourceKey + 1 - edges.size(), std::set<int>());
    edges[sourceKey].insert(destKey);
  }

  size_t size = value2KeyMapping.size();
  std::vector<int> stack;
  stack.reserve(size);
  if (edges.size() < size)
    edges.insert(edges.end(), size - edges.size(), std::set<int>());
  std::vector<bool> visited(size, false);

  // perform a topology sort
  for (size_t i = 0; i < size; i++)
  {
    if (!visited[i])
      sortTopologically(i, edges, visited, stack);
  }

  // translate the keys back to the real values
  typename std::vector<T> result;
  for (const auto& key : stack)
    result.push_back(key2ValueMapping[key]);

  return result;
}

void SpecialSort::sortTopologically(int current,
                                    const std::vector<std::set<int>>& edges,
                                    std::vector<bool>& visited,
                                    std::vector<int>& stack)
{
  // mark the current node as visited
  visited[current] = true;

  for (const auto i : edges.at(current))
  {
    if (!visited[i])
      sortTopologically(i, edges, visited, stack);
  }

  stack.push_back(current);
}
