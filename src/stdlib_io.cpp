#include "stdlib_io.hpp"

namespace sun {

std::string io_runtime_cpp() {
    return R"cpp(
std::string sun_io_read_text(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw SunRuntimeError("io.read_text could not open file '" + path + "'");
    }
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    if (file.bad()) {
        throw SunRuntimeError("io.read_text failed while reading file '" + path + "'");
    }
    return contents;
}

void sun_io_write_text(const std::string& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw SunRuntimeError("io.write_text could not open file '" + path + "'");
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file) {
        throw SunRuntimeError("io.write_text failed while writing file '" + path + "'");
    }
}

void sun_io_append_text(const std::string& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) {
        throw SunRuntimeError("io.append_text could not open file '" + path + "'");
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file) {
        throw SunRuntimeError("io.append_text failed while writing file '" + path + "'");
    }
}

bool sun_io_exists(const std::string& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        throw SunRuntimeError("io.exists failed for '" + path + "': " + error.message());
    }
    return exists;
}

std::vector<std::string> sun_io_read_lines(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw SunRuntimeError("io.read_lines could not open file '" + path + "'");
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    if (file.bad()) {
        throw SunRuntimeError("io.read_lines failed while reading file '" + path + "'");
    }
    return lines;
}

bool sun_io_is_directory(const std::string& path) {
    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error) {
        throw SunRuntimeError("io.is_directory failed for '" + path + "': " + error.message());
    }
    return is_directory;
}

bool sun_io_remove(const std::string& path) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error) {
        throw SunRuntimeError("io.remove failed for '" + path + "': " + error.message());
    }
    return removed;
}

bool sun_io_create_directories(const std::string& path) {
    std::error_code error;
    const bool created = std::filesystem::create_directories(path, error);
    if (error) {
        throw SunRuntimeError("io.create_directories failed for '" + path + "': " +
                              error.message());
    }
    return created;
}

std::int64_t sun_io_file_size(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw SunRuntimeError("io.file_size failed for '" + path + "': " + error.message());
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        throw SunRuntimeError("io.file_size is too large for int");
    }
    return static_cast<std::int64_t>(size);
}
)cpp";
}

} // namespace sun
