#include "FileIO.h"

#include <fstream>
#include <iterator>

IOResult openFile(Document& doc, const std::string& path) {
    if (path.empty()) return IOResult{false, "No filename given."};

    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in) return IOResult{false, "Cannot open '" + path + "': file does not exist or is not readable."};

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return IOResult{false, "Error while reading '" + path + "' (is it a folder?)."};

    doc.loadFromString(content);
    doc.setPath(path);
    doc.markSaved();
    return IOResult{true, "Opened '" + path + "'."};
}

IOResult saveFile(Document& doc, const std::string& path) {
    if (path.empty()) return IOResult{false, "No filename given."};

    std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return IOResult{false, "Cannot write to '" + path + "': check the folder and permissions."};

    std::string content = doc.toString();
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    if (!out) return IOResult{false, "Error while writing '" + path + "' (disk full?)."};

    doc.setPath(path);
    doc.markSaved();
    return IOResult{true, "Saved '" + path + "'."};
}
