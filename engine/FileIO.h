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

// Is 'name' usable as the name of a file or folder (a NAME, not a path)? Rejected: empty,
// "." / "..", a '/' or '\', the characters Windows forbids, control characters, a trailing
// '.' or blank, and names over 255 bytes. ok == false comes with the reason.
IOResult checkFileName(const std::string& name);

// Rename a file on disk to 'newName': a NAME in the same folder, not a path. Nothing is
// overwritten: if another file already has that name the call fails. (Changing only the
// upper/lower case of a name is allowed on systems that ignore case.) On success
// *newPath, if given, receives the full new path. Never throws.
// Rejected names: empty, "." / "..", a '/' or '\', the characters Windows forbids,
// control characters, a trailing '.' or blank, and names over 255 bytes.
IOResult renameFile(const std::string& path, const std::string& newName, std::string* newPath = nullptr);

// ---- file manager (the Explorer sidebar). All of them never overwrite anything and never throw;
// on success *newPath, if given, receives the full new path (separator style of the input kept).

// Like renameFile, but 'path' may also be a folder.
IOResult renamePath(const std::string& path, const std::string& newName, std::string* newPath = nullptr);

// A new empty file / a new folder called 'name' inside 'folder'. Fails if the name is taken.
IOResult createFile(const std::string& folder, const std::string& name, std::string* newPath = nullptr);
IOResult createFolder(const std::string& folder, const std::string& name, std::string* newPath = nullptr);

// Move a file or folder into 'destFolder', keeping its name. Fails if that name is taken there,
// or if a folder would be moved into itself. Moving to its own folder succeeds and does nothing.
// Works across drives (copy, then remove the original).
IOResult movePath(const std::string& path, const std::string& destFolder, std::string* newPath = nullptr);

// Copy a file or folder (with everything in it) into 'destFolder'. If the name is taken the copy
// is called "name copy.ext", "name copy 2.ext", ... (so copying into the same folder duplicates).
IOResult copyPath(const std::string& path, const std::string& destFolder, std::string* newPath = nullptr);

// The name used for the n-th copy: copyName("a.txt", 1) == "a copy.txt", (.., 2) == "a copy 2.txt".
std::string copyName(const std::string& name, int n);

// Is 'path' the folder 'folder' itself or somewhere below it? (Resolves "..", links and case
// as the file system does; the paths need not exist.)
bool isSameOrInside(const std::string& path, const std::string& folder);
