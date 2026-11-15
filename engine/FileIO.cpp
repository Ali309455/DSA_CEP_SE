#include "FileIO.h"

#include <filesystem>
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

    std::ifstream in(std::filesystem::u8path(path), std::ios::in | std::ios::binary);   // UTF-8 paths work on Windows too
    if (!in) return IOResult{false, "Cannot open '" + path + "': file does not exist or is not readable."};

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return IOResult{false, "Error while reading '" + path + "' (is it a folder?)."};

    doc.loadFromString(content);
    doc.setCrlf(content.find("\r\n") != std::string::npos);
    doc.setPath(path);
    doc.markSaved();
    return IOResult{true, "Opened '" + path + "'."};
}

IOResult saveFile(Document& doc, const std::string& path) {
    if (path.empty()) return IOResult{false, "No filename given."};
    std::string why;
    if (badFileName(path, why)) return IOResult{false, why};

    std::ofstream out(std::filesystem::u8path(path), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return IOResult{false, "Cannot write to '" + path + "': check the folder and permissions."};

    std::string content = doc.toString();
    if (doc.crlf()) {                                     // write the line endings the file came with
        std::string crlf;
        crlf.reserve(content.size() + content.size() / 16);
        for (size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n') crlf.push_back('\r');
            crlf.push_back(content[i]);
        }
        content.swap(crlf);
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    if (!out) return IOResult{false, "Error while writing '" + path + "' (disk full?)."};

    doc.setPath(path);
    doc.markSaved();
    return IOResult{true, "Saved '" + path + "'."};
}

// ---------------------------------------------------------------- rename

namespace fs = std::filesystem;

IOResult renameFile(const std::string& path, const std::string& newName, std::string* newPath) {
    if (path.empty()) return IOResult{false, "No filename given."};
    if (newName.empty()) return IOResult{false, "The new name is empty."};
    if (newName == "." || newName == "..") return IOResult{false, "'" + newName + "' is not a valid file name."};
    if (newName.size() > 255) return IOResult{false, "The new name is too long (255 characters at most)."};
    for (size_t i = 0; i < newName.size(); ++i) {
        if (newName[i] == '/' || newName[i] == '\\')
            return IOResult{false, "A new name cannot contain folder separators. To move a file, use your file manager."};
        if (static_cast<unsigned char>(newName[i]) < 32)
            return IOResult{false, "A file name cannot contain control characters."};
    }
    if (newName.back() == '.' || newName.back() == ' ')
        return IOResult{false, "A file name cannot end with a dot or a blank."};
    std::string why;
    if (badFileName("./" + newName, why)) return IOResult{false, why};   // "./" so a leading "c:" is rejected too

    std::error_code ec;
    const fs::path src = fs::u8path(path);
    if (!fs::is_regular_file(src, ec))
        return IOResult{false, "Cannot rename '" + path + "': the file does not exist."};

    const fs::path dst = src.parent_path() / fs::u8path(newName);
    if (src.filename() == dst.filename()) {
        if (newPath) *newPath = path;
        return IOResult{true, "The file already has that name."};
    }
    if (fs::exists(dst, ec)) {
        bool sameFile = fs::equivalent(src, dst, ec) && !ec;     // case-only change on a case-insensitive disk
        if (!sameFile) return IOResult{false, "A file named '" + newName + "' already exists in that folder."};
    }
    ec.clear();
    fs::rename(src, dst, ec);
    if (ec) return IOResult{false, "Cannot rename '" + path + "': " + ec.message() + "."};
    if (newPath) {
        // keep the caller's own separator style (the folder part is copied as it was written)
        size_t cut = path.find_last_of("/\\");
        *newPath = (cut == std::string::npos) ? newName : path.substr(0, cut + 1) + newName;
    }
    return IOResult{true, "Renamed to '" + newName + "'."};
}
