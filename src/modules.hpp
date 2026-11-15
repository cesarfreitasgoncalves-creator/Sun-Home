#pragma once

#include <filesystem>
#include <string>

namespace sun {

std::string load_project_source(const std::filesystem::path& source_path);

} // namespace sun
