#include <filesystem>
#include <fstream>
#include <string>

#include "dream/runtime/boot/outputs.h"

#include "doctest.h"

using dream::boot::output_file_problem;
using dream::boot::same_physical;

TEST_CASE("boot: the relocation hint compares physical addresses") {
    // An image based at a P0 address, 0x0C..., and a target the game called through P1, 0x8C...,
    // are the same bytes; comparing the full addresses called that a copy from one to the other.
    CHECK(same_physical(0x0C0221B8u, 0x8C0221B8u));
    CHECK(same_physical(0xAC004000u, 0x0C004000u));
    CHECK_FALSE(same_physical(0x8C0221B8u, 0x8C0221BCu));
    CHECK_FALSE(same_physical(0x8C0221B8u, 0x8C0321B8u));
}

TEST_CASE("boot: an output path whose folder is missing is refused before the run") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "dream-boot-outputs-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(output_file_problem((dir / "report.txt").string()) == "");
    CHECK(output_file_problem("report.txt") == "");  // the current folder
    const std::string missing = output_file_problem((dir / "logs" / "report.txt").string());
    CHECK(missing.find("doesn't exist") != std::string::npos);
    CHECK(missing.find((dir / "logs").string()) != std::string::npos);
    CHECK(output_file_problem(dir.string()) == "it is a folder");
    {
        std::ofstream(dir / "file") << "x";
    }
    const std::string through_file = output_file_problem((dir / "file" / "report.txt").string());
    CHECK(through_file.find("is not a folder") != std::string::npos);
    fs::remove_all(dir);
}
