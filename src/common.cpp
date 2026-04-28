#include "common.h"

#include <array>
#include <stdexcept>
#include <sstream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <vector>
#endif

#ifdef __linux__
#include <unistd.h>
#endif

namespace dosbundle {

namespace {

fs::path resolve_platform_executable_path()
{
#ifdef __linux__
	std::array<char, 4096> buffer = {};
	const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
	if (length < 0) {
		throw std::runtime_error("unable to resolve executable path from /proc/self/exe");
	}
	buffer[static_cast<std::size_t>(length)] = '\0';
	return fs::path(buffer.data()).lexically_normal();
#elif defined(__APPLE__)
	std::uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	if (size == 0) {
		throw std::runtime_error("unable to determine macOS executable path buffer size");
	}

	std::vector<char> buffer(size);
	if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
		throw std::runtime_error("unable to resolve executable path via _NSGetExecutablePath");
	}

	return fs::weakly_canonical(fs::path(buffer.data()));
#else
	throw std::runtime_error("platform executable path lookup is not implemented");
#endif
}

} // namespace

std::string trim(const std::string& value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

bool starts_with(const std::string& value, std::string_view prefix)
{
	return value.rfind(prefix.data(), 0) == 0;
}

std::vector<std::string> split(const std::string& value, char separator)
{
	std::vector<std::string> parts = {};
	std::stringstream stream(value);
	std::string item = {};

	while (std::getline(stream, item, separator)) {
		parts.push_back(item);
	}
	return parts;
}

fs::path resolve_executable_path(const char* argv0)
{
	if (!argv0 || !*argv0) {
		throw std::runtime_error("missing argv[0] for executable path resolution");
	}

	const fs::path candidate(argv0);
	if (candidate.is_absolute() || candidate.has_parent_path()) {
		return fs::absolute(candidate).lexically_normal();
	}

	return resolve_platform_executable_path();
}

} // namespace dosbundle
