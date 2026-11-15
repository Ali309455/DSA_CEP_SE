// Workspace.h - finding files in a folder (phase 5, "Quick Open"). No GUI code.
//
//   listWorkspaceFiles : every file under a folder, as relative paths
//   fuzzyScore         : how well a short query matches a path ("mw" ~ "src/MainWindow.cpp")
//   rankFiles          : the best k paths for a query, without sorting the whole list
#pragma once
#include <cstddef>
#include <string>
#include "DynArray.h"

// All regular files below 'root', as paths relative to it with '/' separators. Skips folders
// whose name starts with '.', node_modules, build, __pycache__ and cmake-build-*, and files
// that are obviously not text (images, objects, executables, archives). Stops after
// 'maxFiles' files. A missing or unreadable folder gives an empty list; never throws.
// The order is the order the file system returns them.
DynArray<std::string> listWorkspaceFiles(const std::string& root, size_t maxFiles = 20000);

// Does 'query' match 'candidate' as a subsequence, ignoring case ("mw" in "MainWindow")?
// -1 if not. Otherwise a score >= 0, higher = better: matches at the start of the path, of a
// path segment or of a word (after _ - . or in camelCase) score more, characters that follow
// each other score more, characters in the file name count more than in the folders, and
// every skipped character costs a little. Blanks in the query are ignored; an empty query
// scores 0. Dynamic programming over (query character, candidate character): O(q * n).
int fuzzyScore(const std::string& query, const std::string& candidate);

// Indexes into 'files' of the best matches for 'query', best first, at most 'maxResults'.
// Ties: shorter path first, then alphabetical. Files that do not match are left out.
// An empty query returns the first 'maxResults' files in their own order.
// O(n * (q * len + k)): a small sorted array of the k best is kept, the list is never sorted.
DynArray<size_t> rankFiles(const DynArray<std::string>& files, const std::string& query, size_t maxResults = 50);
