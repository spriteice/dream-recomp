// What a launcher checks before it runs. A path given for an output file is checked here, so a run
// fails at the start, naming the flag and the reason, rather than after it played with nothing
// written and exit 0; the report is written last of all, which is where that bit. The address
// comparison is the one the report's relocation hint needs: the image's base and a target the game
// called can name the same bytes through different SH-4 mappings.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace dream::boot {

// Why `path` cannot be written as an output file, or "" when nothing is known to stand in the way:
// its folder has to exist and be a folder, and the path itself must not be one. Nothing is opened,
// created or truncated here; the writer still checks its own open and write.
inline std::string output_file_problem(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    if (fs::is_directory(p, ec))
        return "it is a folder";
    const fs::path dir = p.parent_path();
    if (dir.empty())
        return "";  // the current folder, which exists
    if (!fs::exists(dir, ec))
        return "the folder " + dir.string() + " doesn't exist";
    if (!fs::is_directory(dir, ec))
        return dir.string() + " is not a folder";
    return "";
}

// Whether two SH-4 addresses name the same physical location. The top three bits choose the
// mapping (P0 to P4, cached or not): 0x0C0221B8, 0x8C0221B8 and 0xAC0221B8 are one word of RAM.
constexpr bool same_physical(std::uint32_t a, std::uint32_t b) {
    return ((a ^ b) & 0x1FFFFFFFu) == 0;
}

}  // namespace dream::boot
