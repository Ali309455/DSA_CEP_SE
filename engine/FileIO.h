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
