#pragma once

#include <filesystem>
#include <string_view>

namespace lumiere
{

inline constexpr std::string_view SOURCE_FILE_EXTENSION = ".lum";

inline bool is_source_file(const std::filesystem::path &path)
{
    return path.extension() == SOURCE_FILE_EXTENSION;
}

inline bool is_test_source_file(const std::filesystem::path &path)
{
    return is_source_file(path) && path.stem().string().ends_with("_test");
}

} // namespace lumiere
