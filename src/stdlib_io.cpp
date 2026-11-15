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
)cpp";
}

} // namespace sun
