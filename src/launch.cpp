#include "launch.h"

#include "common.h"
#include "config.h"

#include <cstdlib>
#include <cstdio>
#include <regex>
#include <sstream>
#include <stdexcept>

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dosbundle {

namespace {

constexpr int min_supported_major = 0;
constexpr int min_supported_minor = 82;
constexpr int min_supported_patch = 0;

std::optional<fs::path> find_executable_in_path(const std::string& executable_name)
{
	const char* path_value = std::getenv("PATH");
	if (!path_value || std::string(path_value).empty()) {
		return std::nullopt;
	}

	for (const auto& entry : split(path_value, ':')) {
		if (entry.empty()) {
			continue;
		}
		const auto candidate = fs::path(entry) / executable_name;
		if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
			return fs::canonical(candidate);
		}
	}

	return std::nullopt;
}

std::string shell_quote(const std::string& value)
{
	std::string quoted = "'";
	for (const char ch : value) {
		if (ch == '\'') {
			quoted += "'\\''";
		} else {
			quoted.push_back(ch);
		}
	}
	quoted.push_back('\'');
	return quoted;
}

std::string read_process_stdout(const std::vector<std::string>& args)
{
	std::ostringstream command;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) {
			command << ' ';
		}
		command << shell_quote(args[i]);
	}
	command << " 2>&1";

#ifdef _WIN32
	throw std::runtime_error("process probing is not implemented on Windows yet");
#else
	FILE* pipe = popen(command.str().c_str(), "r");
	if (!pipe) {
		throw std::runtime_error("failed to start process for probing");
	}

	std::string output = {};
	char buffer[256];
	while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
		output += buffer;
	}

	const int exit_code = pclose(pipe);
	if (exit_code != 0) {
		throw std::runtime_error("process probe failed: " + trim(output));
	}

	return output;
#endif
}

bool is_supported_or_newer(const DosboxVersion& version)
{
	if (version.major != min_supported_major) {
		return version.major > min_supported_major;
	}
	if (version.minor != min_supported_minor) {
		return version.minor > min_supported_minor;
	}
	return version.patch >= min_supported_patch;
}

} // namespace

std::optional<fs::path> discover_dosbox_path()
{
	const auto& config = load_app_config();
	if (config.dosbox_path && fs::exists(*config.dosbox_path) &&
	    fs::is_regular_file(*config.dosbox_path)) {
		return fs::canonical(*config.dosbox_path);
	}

	const std::vector<fs::path> explicit_candidates = {
	        "/usr/local/bin/dosbox-staging",
	        "/opt/homebrew/bin/dosbox-staging",
	        "/usr/local/bin/dosbox",
	        "/opt/homebrew/bin/dosbox",
	};

	for (const auto& candidate : explicit_candidates) {
		if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
			return fs::canonical(candidate);
		}
	}

	for (const auto* name : {"dosbox-staging", "dosbox"}) {
		if (const auto path = find_executable_in_path(name)) {
			return path;
		}
	}

	return std::nullopt;
}

DosboxVersion probe_dosbox_version(const fs::path& dosbox_path)
{
	const auto output = read_process_stdout({dosbox_path.string(), "--version"});
	if (output.find("DOSBox Staging") == std::string::npos &&
	    output.find("dosbox-staging") == std::string::npos) {
		throw std::runtime_error("executable does not appear to be DOSBox Staging: " +
		                         dosbox_path.string());
	}

	std::regex version_pattern(R"(version\s+(\d+)\.(\d+)\.(\d+))",
	                           std::regex_constants::icase);
	std::smatch match;
	if (!std::regex_search(output, match, version_pattern)) {
		throw std::runtime_error("unable to parse DOSBox Staging version output");
	}

	return DosboxVersion{
	        .raw_output = trim(output),
	        .major = std::stoi(match[1].str()),
	        .minor = std::stoi(match[2].str()),
	        .patch = std::stoi(match[3].str()),
	};
}

void validate_dosbox_version(const DosboxVersion& version)
{
	if (!is_supported_or_newer(version)) {
		throw std::runtime_error("DOSBox Staging version " + std::to_string(version.major) + "." +
		                         std::to_string(version.minor) + "." +
		                         std::to_string(version.patch) +
		                         " is too old; require at least 0.82.0");
	}
}

std::vector<std::string> make_launch_args(const fs::path& dosbox_path,
                                          const BundleMetadata& bundle)
{
	return {
	        dosbox_path.string(),
	        "--noprimaryconf",
	        "--nolocalconf",
	        "--conf",
	        bundle.config_path.string(),
	        bundle.startup_path.string(),
	};
}

std::string format_command(const std::vector<std::string>& args)
{
	std::ostringstream output;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) {
			output << ' ';
		}
		output << shell_quote(args[i]);
	}
	return output.str();
}

int spawn_and_wait(const std::vector<std::string>& args)
{
#ifdef _WIN32
	throw std::runtime_error("launch execution is not implemented on Windows yet");
#else
	std::vector<char*> raw_args = {};
	raw_args.reserve(args.size() + 1);

	for (const auto& arg : args) {
		raw_args.push_back(const_cast<char*>(arg.c_str()));
	}
	raw_args.push_back(nullptr);

	pid_t child_pid = 0;
	const int spawn_result = posix_spawn(&child_pid,
	                                     raw_args.front(),
	                                     nullptr,
	                                     nullptr,
	                                     raw_args.data(),
	                                     environ);
	if (spawn_result != 0) {
		throw std::runtime_error("failed to launch DOSBox Staging");
	}

	int status = 0;
	if (waitpid(child_pid, &status, 0) < 0) {
		throw std::runtime_error("failed to wait for DOSBox Staging");
	}
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	}
	return 1;
#endif
}

} // namespace dosbundle
