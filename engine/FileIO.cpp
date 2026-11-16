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

// ---------------------------------------------------------------- names, rename

namespace fs = std::filesystem;

IOResult checkFileName(const std::string& name) {
    if (name.empty()) return IOResult{false, "The name is empty."};
    if (name == "." || name == "..") return IOResult{false, "'" + name + "' is not a valid name."};
    if (name.size() > 255) return IOResult{false, "The name is too long (255 characters at most)."};
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '/' || name[i] == '\\')
            return IOResult{false, "A name cannot contain folder separators (/ or \\). To move an item, drag it onto another folder in the Explorer."};
        if (static_cast<unsigned char>(name[i]) < 32)
            return IOResult{false, "A name cannot contain control characters."};
    }
    if (name.back() == '.' || name.back() == ' ')
        return IOResult{false, "A name cannot end with a dot or a blank."};
    std::string why;
    if (badFileName("./" + name, why)) return IOResult{false, why};   // "./" so a leading "c:" is rejected too
    return IOResult{true, ""};
}

// 'folder' + '/' + 'name', keeping the separator style the caller used for the folder.
static std::string joinPath(const std::string& folder, const std::string& name) {
    if (folder.empty()) return name;
    const char last = folder.back();
    if (last == '/' || last == '\\') return folder + name;
    const char sep = (folder.find('\\') != std::string::npos && folder.find('/') == std::string::npos) ? '\\' : '/';
    return folder + sep + name;
}

static std::string nameOf(const std::string& path) {
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) --end;
    const size_t cut = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
    return cut == std::string::npos ? path.substr(0, end) : path.substr(cut + 1, end - cut - 1);
}

IOResult renamePath(const std::string& path, const std::string& newName, std::string* newPath) {
    if (path.empty()) return IOResult{false, "No path given."};
    IOResult ok = checkFileName(newName);
    if (!ok.ok) return ok;

    std::error_code ec;
    const fs::path src = fs::u8path(path);
    if (!fs::exists(src, ec)) return IOResult{false, "Cannot rename '" + path + "': it does not exist."};

    const fs::path dst = src.parent_path() / fs::u8path(newName);
    if (src.filename() == dst.filename()) {
        if (newPath) *newPath = path;
        return IOResult{true, "It already has that name."};
    }
    if (fs::exists(dst, ec)) {
        bool sameItem = fs::equivalent(src, dst, ec) && !ec;     // case-only change on a case-insensitive disk
        if (!sameItem) return IOResult{false, "'" + newName + "' already exists in that folder."};
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

IOResult renameFile(const std::string& path, const std::string& newName, std::string* newPath) {
    if (path.empty()) return IOResult{false, "No filename given."};
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(path), ec)) {
        IOResult nameOk = checkFileName(newName);                // report a bad name first, as before
        if (!nameOk.ok) return nameOk;
        return IOResult{false, "Cannot rename '" + path + "': the file does not exist."};
    }
    return renamePath(path, newName, newPath);
}

// ---------------------------------------------------------------- file manager

bool isSameOrInside(const std::string& path, const std::string& folder) {
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::u8path(path), ec);
    if (ec) return false;
    const fs::path f = fs::weakly_canonical(fs::u8path(folder), ec);
    if (ec) return false;
    auto pi = p.begin(), fi = f.begin();
    for (; fi != f.end(); ++fi, ++pi) {
        if (fi->empty()) continue;                     // trailing separator gives an empty last part
        if (pi == p.end() || *pi != *fi) return false;
    }
    return true;
}

static IOResult createItem(const std::string& folder, const std::string& name, bool asFolder, std::string* newPath) {
    IOResult ok = checkFileName(name);
    if (!ok.ok) return ok;
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(folder), ec)) return IOResult{false, "The folder '" + folder + "' does not exist."};
    const std::string target = joinPath(folder, name);
    const fs::path t = fs::u8path(target);
    if (fs::exists(t, ec)) return IOResult{false, "'" + name + "' already exists in that folder."};
    if (asFolder) {
        if (!fs::create_directory(t, ec) || ec) return IOResult{false, "Cannot create the folder '" + name + "': " + ec.message() + "."};
    } else {
        std::ofstream out(t, std::ios::out | std::ios::binary);
        if (!out) return IOResult{false, "Cannot create '" + name + "': check the folder and permissions."};
    }
    if (newPath) *newPath = target;
    return IOResult{true, std::string(asFolder ? "Created folder '" : "Created '") + name + "'."};
}

IOResult createFile(const std::string& folder, const std::string& name, std::string* newPath) {
    return createItem(folder, name, false, newPath);
}

IOResult createFolder(const std::string& folder, const std::string& name, std::string* newPath) {
    return createItem(folder, name, true, newPath);
}

IOResult movePath(const std::string& path, const std::string& destFolder, std::string* newPath) {
    if (path.empty() || destFolder.empty()) return IOResult{false, "No path given."};
    std::error_code ec;
    const fs::path src = fs::u8path(path);
    if (!fs::exists(src, ec)) return IOResult{false, "Cannot move '" + path + "': it does not exist."};
    if (!fs::is_directory(fs::u8path(destFolder), ec)) return IOResult{false, "The folder '" + destFolder + "' does not exist."};
    const std::string name = nameOf(path);
    const std::string target = joinPath(destFolder, name);
    if (fs::is_directory(src, ec) && isSameOrInside(destFolder, path))
        return IOResult{false, "Cannot move the folder '" + name + "' into itself."};
    if (fs::exists(fs::u8path(target), ec)) {
        if (fs::equivalent(src, fs::u8path(target), ec) && !ec) {      // dropped on its own folder: nothing to do
            if (newPath) *newPath = path;
            return IOResult{true, "It is already in that folder."};
        }
        return IOResult{false, "'" + name + "' already exists in the destination folder."};
    }
    ec.clear();
    fs::rename(src, fs::u8path(target), ec);
    if (ec) {                                          // another drive: copy, then remove the original
        std::error_code ec2;
        fs::copy(src, fs::u8path(target), fs::copy_options::recursive, ec2);
        if (ec2) { fs::remove_all(fs::u8path(target), ec2); return IOResult{false, "Cannot move '" + name + "': " + ec.message() + "."}; }
        fs::remove_all(src, ec2);
    }
    if (newPath) *newPath = target;
    return IOResult{true, "Moved '" + name + "'."};
}

std::string copyName(const std::string& name, int n) {
    // "notes.txt" -> "notes copy.txt", "notes copy 2.txt", ...  A leading dot (".gitignore") is not an extension.
    size_t dot = name.find_last_of('.');
    if (dot == 0 || dot == std::string::npos) dot = name.size();
    const std::string suffix = n <= 1 ? " copy" : " copy " + std::to_string(n);
    return name.substr(0, dot) + suffix + name.substr(dot);
}

IOResult copyPath(const std::string& path, const std::string& destFolder, std::string* newPath) {
    if (path.empty() || destFolder.empty()) return IOResult{false, "No path given."};
    std::error_code ec;
    const fs::path src = fs::u8path(path);
    if (!fs::exists(src, ec)) return IOResult{false, "Cannot copy '" + path + "': it does not exist."};
    if (!fs::is_directory(fs::u8path(destFolder), ec)) return IOResult{false, "The folder '" + destFolder + "' does not exist."};
    const bool isDir = fs::is_directory(src, ec);
    if (isDir && isSameOrInside(destFolder, path))
        return IOResult{false, "Cannot copy the folder '" + nameOf(path) + "' into itself."};
    const std::string name = nameOf(path);
    std::string target = joinPath(destFolder, name);
    for (int n = 1; fs::exists(fs::u8path(target), ec); ++n) {    // never overwrite: "name copy", "name copy 2", ...
        if (n > 9999) return IOResult{false, "Cannot find a free name for the copy."};
        target = joinPath(destFolder, copyName(name, n));
    }
    if (isDir) fs::copy(src, fs::u8path(target), fs::copy_options::recursive, ec);
    else fs::copy_file(src, fs::u8path(target), ec);
    if (ec) {
        std::error_code ec2;
        fs::remove_all(fs::u8path(target), ec2);
        return IOResult{false, "Cannot copy '" + name + "': " + ec.message() + "."};
    }
    if (newPath) *newPath = target;
    return IOResult{true, "Copied to '" + nameOf(target) + "'."};
}
