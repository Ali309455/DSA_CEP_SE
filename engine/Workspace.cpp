#include "Workspace.h"
#include <climits>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- listing

static std::string lowerStr(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i)
        if (r[i] >= 'A' && r[i] <= 'Z') r[i] = static_cast<char>(r[i] - 'A' + 'a');
    return r;
}

static bool skipDirectory(const std::string& name) {
    if (name.empty() || name[0] == '.') return true;
    return name == "node_modules" || name == "build" || name == "__pycache__" || name.compare(0, 12, "cmake-build-") == 0;
}

static bool skipFile(const std::string& name) {
    static const char* const bad[] = {
        ".o", ".obj", ".exe", ".dll", ".so", ".dylib", ".a", ".lib", ".pdb", ".class", ".pyc",
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".webp", ".pdf", ".zip", ".gz", ".7z", ".rar", ".tar",
        ".mp3", ".mp4", ".wav", ".avi", ".mov", ".ttf", ".otf", ".woff", ".woff2", ".bin", ".dat"};
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = lowerStr(name.substr(dot));
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (ext == bad[i]) return true;
    return false;
}

DynArray<std::string> listWorkspaceFiles(const std::string& root, size_t maxFiles) {
    DynArray<std::string> out;
    if (root.empty() || maxFiles == 0) return out;
    std::error_code ec;
    const fs::path base = fs::u8path(root);
    if (!fs::is_directory(base, ec)) return out;

    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return out;
    while (it != end && out.size() < maxFiles) {
        const fs::directory_entry& e = *it;
        std::error_code ec2;
        std::string name = e.path().filename().u8string();
        if (e.is_directory(ec2)) {
            if (skipDirectory(name)) it.disable_recursion_pending();
        } else if (e.is_regular_file(ec2) && !skipFile(name)) {
            std::error_code ec3;
            fs::path rel = fs::relative(e.path(), base, ec3);
            if (!ec3) out.push_back(rel.generic_u8string());
        }
        it.increment(ec2);
        if (ec2) break;
    }
    return out;
}

// ---------------------------------------------------------------- fuzzy score

static bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
static bool isLower(char c) { return c >= 'a' && c <= 'z'; }

int fuzzyScore(const std::string& queryIn, const std::string& candIn) {
    std::string cand = candIn;                               // '\\' and '/' are the same separator
    for (size_t i = 0; i < cand.size(); ++i) if (cand[i] == '\\') cand[i] = '/';
    std::string q;
    for (size_t i = 0; i < queryIn.size(); ++i) {
        char c = queryIn[i];
        if (c == ' ' || c == '\t') continue;
        q.push_back(c == '\\' ? '/' : c);
    }
    if (q.empty()) return 0;
    const size_t n = cand.size();
    if (q.size() > n) return -1;

    const std::string ql = lowerStr(q);
    const std::string cl = lowerStr(cand);
    const size_t lastSlash = cand.find_last_of("/\\");
    const size_t nameStart = (lastSlash == std::string::npos) ? 0 : lastSlash + 1;

    // bonus[j]: what a match at candidate position j is worth by itself
    DynArray<int> bonus;
    for (size_t j = 0; j < n; ++j) {
        int b = 0;
        if (j == 0 || cand[j - 1] == '/' || cand[j - 1] == '\\') b = 16;            // start of a segment
        else if (cand[j - 1] == '_' || cand[j - 1] == '-' || cand[j - 1] == '.' || cand[j - 1] == ' ') b = 10;
        else if (isLower(cand[j - 1]) && isUpper(cand[j])) b = 8;                    // camelCase hump
        if (j >= nameStart) b += 4;                                                  // in the file name
        bonus.push_back(b);
    }

    const int NEG = INT_MIN / 2;
    // M[j]: best score with the current query char matched exactly at j.
    // D[j]: best score with it matched at some k <= j, each skipped char after k costing 1.
    DynArray<int> prevM, prevD, curM, curD;
    for (size_t j = 0; j < n; ++j) { prevM.push_back(NEG); prevD.push_back(NEG); curM.push_back(NEG); curD.push_back(NEG); }

    for (size_t i = 0; i < q.size(); ++i) {
        for (size_t j = 0; j < n; ++j) {
            int m = NEG;
            if (cl[j] == ql[i]) {
                int base = 1 + bonus[j] + (cand[j] == q[i] ? 1 : 0);
                int best = NEG;
                if (i == 0) {
                    best = 0;
                } else if (j > 0) {
                    if (prevM[j - 1] > NEG) best = prevM[j - 1] + 12;        // follows the previous match directly
                    if (prevD[j - 1] > best) best = prevD[j - 1];
                }
                if (best > NEG) m = base + best;
            }
            curM[j] = m;
            int d = (j > 0 && curD[j - 1] > NEG) ? curD[j - 1] - 1 : NEG;
            curD[j] = (m > d) ? m : d;
        }
        prevM.swap(curM);
        prevD.swap(curD);
    }
    int result = prevD[n - 1];
    if (result <= NEG) return -1;
    return result < 0 ? 0 : result;
}

// ---------------------------------------------------------------- ranking

// Is (scoreA, a) a better hit than (scoreB, b)?
static bool better(int scoreA, const std::string& a, int scoreB, const std::string& b) {
    if (scoreA != scoreB) return scoreA > scoreB;
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

DynArray<size_t> rankFiles(const DynArray<std::string>& files, const std::string& query, size_t maxResults) {
    DynArray<size_t> best;
    if (maxResults == 0) return best;
    bool blank = true;
    for (size_t i = 0; i < query.size(); ++i) if (query[i] != ' ' && query[i] != '\t') { blank = false; break; }
    if (blank) {
        for (size_t i = 0; i < files.size() && i < maxResults; ++i) best.push_back(i);
        return best;
    }

    DynArray<int> scores;                                  // scores[k] belongs to best[k]
    for (size_t i = 0; i < files.size(); ++i) {
        int s = fuzzyScore(query, files[i]);
        if (s < 0) continue;
        if (best.size() == maxResults && !better(s, files[i], scores[maxResults - 1], files[best[maxResults - 1]])) continue;
        size_t pos = best.size();                          // insertion sort into the small array
        while (pos > 0 && better(s, files[i], scores[pos - 1], files[best[pos - 1]])) --pos;
        best.insert(pos, i);
        scores.insert(pos, s);
        if (best.size() > maxResults) { best.pop_back(); scores.pop_back(); }
    }
    return best;
}
