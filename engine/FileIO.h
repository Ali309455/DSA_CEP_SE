// FileIO.h - open / save documents. Never throws: problems come back in the result.
#pragma once
#include <string>
#include "Document.h"

struct IOResult {
    bool ok;
    std::string message;
};

// Replace the document's content with the file's content. On success the
// document's path is set and modified() is false. On failure the document is
// left untouched.
IOResult openFile(Document& doc, const std::string& path);

// Write the document to 'path'. On success the document's path becomes 'path'
// and modified() is false. (Save-as is just save with a new path.)
IOResult saveFile(Document& doc, const std::string& path);

// Rename a file on disk to 'newName': a NAME in the same folder, not a path. Nothing is
// overwritten: if another file already has that name the call fails. (Changing only the
// upper/lower case of a name is allowed on systems that ignore case.) On success
// *newPath, if given, receives the full new path. Never throws.
// Rejected names: empty, "." / "..", a '/' or '\', the characters Windows forbids,
// control characters, a trailing '.' or blank, and names over 255 bytes.
IOResult renameFile(const std::string& path, const std::string& newName, std::string* newPath = nullptr);
