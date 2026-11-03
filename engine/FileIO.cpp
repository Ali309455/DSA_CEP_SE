#include "FileIO.h"

#include <fstream>
#include <iterator>

// Characters Windows does not allow in a file name. ':' is only fine as the
// drive separator in the 2nd position (C:\...). Rejecting these everywhere gives
// the same clear message on every system.
static bool badFileName(const std::string& path, std::string& why) {
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        bool bad = (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*');
        if (c == ':' && !(i == 1 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))))
            bad = true;
        if (bad) {
            why = std::string("A file name cannot contain the character '") + c +
                  "'. (Placeholders like <file> in the help are not typed: write the plain name, e.g. notes.txt)";
            return true;
        }
    }
    return false;
}

IOResult openFile(Document& doc, const std::string& path) {
    if (path.empty()) return IOResult{false, "No filename given."};
    std::string why;
    if (badFileName(path, why)) return IOResult{false, why};

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
    std::string why;
    if (badFileName(path, why)) return IOResult{false, why};

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
