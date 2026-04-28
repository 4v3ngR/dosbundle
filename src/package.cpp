#include "package.h"

#include "config.h"
#include "launch.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace dosbundle {

namespace {

using FooterMagic = std::array<char, 16>;
constexpr FooterMagic footer_magic = {'D', 'O', 'S', 'B', 'U', 'N', 'D', 'L',
                                      'E', '_', 'S', 'F', 'X', '1', '\0', '\0'};

struct Footer {
	FooterMagic magic = footer_magic;
	std::uint64_t archive_size = 0;
};

constexpr std::size_t footer_size = sizeof(Footer);

fs::path canonical_or_absolute(const fs::path& path)
{
	if (fs::exists(path)) {
		return fs::canonical(path);
	}
	return fs::absolute(path).lexically_normal();
}

std::uint64_t file_size_u64(const fs::path& path)
{
	return static_cast<std::uint64_t>(fs::file_size(path));
}

Footer read_footer(std::ifstream& input)
{
	Footer footer = {};
	input.read(reinterpret_cast<char*>(&footer), sizeof(footer));
	if (static_cast<std::size_t>(input.gcount()) != sizeof(footer)) {
		throw std::runtime_error("unable to read self-extract footer");
	}
	return footer;
}

bool footer_has_valid_magic(const Footer& footer)
{
	return footer.magic == footer_magic;
}

void copy_stream_range(std::ifstream& input,
                       std::ofstream& output,
                       std::uint64_t size_bytes)
{
	std::array<char, 64 * 1024> buffer = {};
	std::uint64_t remaining = size_bytes;

	while (remaining > 0) {
		const auto chunk_size =
		        static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), remaining));
		input.read(buffer.data(), chunk_size);
		if (input.gcount() != chunk_size) {
			throw std::runtime_error("unexpected EOF while copying embedded archive");
		}
		output.write(buffer.data(), chunk_size);
		if (!output) {
			throw std::runtime_error("failed writing embedded archive");
		}
		remaining -= static_cast<std::uint64_t>(chunk_size);
	}
}

void copy_file_contents(const fs::path& source,
                        const fs::path& destination,
                        bool append = false)
{
	std::ifstream input(source, std::ios::binary);
	if (!input) {
		throw std::runtime_error("unable to open source file: " + source.string());
	}

	auto mode = std::ios::binary | std::ios::out;
	if (append) {
		mode |= std::ios::app;
	}
	std::ofstream output(destination, mode);
	if (!output) {
		throw std::runtime_error("unable to open destination file: " + destination.string());
	}

	output << input.rdbuf();
	if (!output) {
		throw std::runtime_error("failed while copying file contents");
	}
}

void append_footer(const fs::path& output_path, const Footer& footer)
{
	std::ofstream output(output_path, std::ios::binary | std::ios::app);
	if (!output) {
		throw std::runtime_error("unable to append footer to output: " + output_path.string());
	}

	output.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
	if (!output) {
		throw std::runtime_error("failed while writing self-extract footer");
	}
}

void run_tar_command(const std::vector<std::string>& args)
{
	const auto exit_code = spawn_and_wait(args);
	if (exit_code != 0) {
		throw std::runtime_error("tar command failed with exit code " +
		                         std::to_string(exit_code));
	}
}

fs::path create_temp_directory(const std::optional<fs::path>& extract_root_override)
{
	const auto& config = load_app_config();
	const fs::path base_dir = extract_root_override ? *extract_root_override : [&]() {
		const auto extract_root_env = std::getenv("DOSBUNDLE_EXTRACT_ROOT");
		return extract_root_env && *extract_root_env ? fs::path(extract_root_env)
		                                             : (config.extract_root ? *config.extract_root
		                                                                    : fs::temp_directory_path());
	}();
	fs::create_directories(base_dir);

#ifdef _WIN32
	throw std::runtime_error("temporary extraction is not implemented on Windows yet");
#else
	auto pattern = (base_dir / "dosbundle-XXXXXX").string();
	std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
	mutable_pattern.push_back('\0');
	char* created = mkdtemp(mutable_pattern.data());
	if (!created) {
		throw std::runtime_error("unable to create temporary extraction directory");
	}
	return fs::path(created);
#endif
}

fs::path extract_embedded_archive(const fs::path& executable_path, const fs::path& extract_root)
{
	const auto archive_path = extract_root / "payload.tar.gz";
	const auto bundle_path = extract_root / "bundle";
	fs::create_directories(bundle_path);

	std::ifstream executable(executable_path, std::ios::binary);
	if (!executable) {
		throw std::runtime_error("unable to open self-extracting executable: " +
		                         executable_path.string());
	}

	const auto total_size = file_size_u64(executable_path);
	if (total_size < footer_size) {
		throw std::runtime_error("self-extracting executable is too small");
	}

	executable.seekg(static_cast<std::streamoff>(total_size - footer_size));
	const auto footer = read_footer(executable);
	if (!footer_has_valid_magic(footer)) {
		throw std::runtime_error("embedded bundle footer not found");
	}
	if (footer.archive_size > total_size - footer_size) {
		throw std::runtime_error("embedded bundle footer is corrupt");
	}

	const auto archive_offset = total_size - footer_size - footer.archive_size;
	executable.seekg(static_cast<std::streamoff>(archive_offset));

	std::ofstream archive_output(archive_path, std::ios::binary);
	if (!archive_output) {
		throw std::runtime_error("unable to write temporary archive: " + archive_path.string());
	}
	copy_stream_range(executable, archive_output, footer.archive_size);
	archive_output.close();

	const auto& config = load_app_config();
	const auto tar_path = config.tar_path ? *config.tar_path : fs::path("/usr/bin/tar");
	run_tar_command({
		        tar_path.string(),
		        "-xzf",
		        archive_path.string(),
		        "-C",
		        bundle_path.string(),
	});

	fs::remove(archive_path);
	return bundle_path;
}

int launch_extracted_bundle(const fs::path& bundle_path,
                            const SelfExtractOptions& options)
{
	const auto bundle = load_bundle_metadata(bundle_path);
	const auto dosbox_path = options.dosbox_override ? *options.dosbox_override : [&]() -> fs::path {
		const auto discovered = discover_dosbox_path();
		if (!discovered) {
			throw std::runtime_error(
			        "unable to find dosbox-staging; use --dosbox <path> or install it");
		}
		return *discovered;
	}();

	if (!fs::exists(dosbox_path) || !fs::is_regular_file(dosbox_path)) {
		throw std::runtime_error("dosbox executable does not exist: " + dosbox_path.string());
	}

	const auto version = probe_dosbox_version(dosbox_path);
	validate_dosbox_version(version);
	const auto launch_args = make_launch_args(dosbox_path, bundle);

	std::cout << "dosbox: " << dosbox_path << '\n';
	std::cout << "version: " << version.raw_output << '\n';
	std::cout << "command: " << format_command(launch_args) << '\n';

	if (options.dry_run) {
		return 0;
	}

	return spawn_and_wait(launch_args);
}

} // namespace

bool executable_has_embedded_bundle(const fs::path& executable_path)
{
	if (!fs::exists(executable_path) || !fs::is_regular_file(executable_path)) {
		return false;
	}

	const auto total_size = file_size_u64(executable_path);
	if (total_size < footer_size) {
		return false;
	}

	std::ifstream input(executable_path, std::ios::binary);
	if (!input) {
		return false;
	}

	input.seekg(static_cast<std::streamoff>(total_size - footer_size));
	const auto footer = read_footer(input);
	return footer_has_valid_magic(footer) && footer.archive_size <= total_size - footer_size;
}

void create_self_extracting_bundle(const fs::path& launcher_path,
                                   const fs::path& bundle_path,
                                   const fs::path& output_path)
{
	const auto resolved_launcher = canonical_or_absolute(launcher_path);
	const auto resolved_bundle = canonical_or_absolute(bundle_path);
	const auto resolved_output = fs::absolute(output_path).lexically_normal();

	if (!fs::exists(resolved_launcher) || !fs::is_regular_file(resolved_launcher)) {
		throw std::runtime_error("launcher does not exist: " + resolved_launcher.string());
	}
	if (!fs::exists(resolved_bundle) || !fs::is_directory(resolved_bundle)) {
		throw std::runtime_error("bundle directory does not exist: " + resolved_bundle.string());
	}
	if (fs::exists(resolved_output)) {
		throw std::runtime_error("output file already exists: " + resolved_output.string());
	}

	const auto temp_root = create_temp_directory(std::nullopt);
	const auto archive_path = temp_root / "bundle.tar.gz";

	try {
		const auto& config = load_app_config();
		const auto tar_path = config.tar_path ? *config.tar_path : fs::path("/usr/bin/tar");
		run_tar_command({
		        tar_path.string(),
		        "-czf",
		        archive_path.string(),
		        "-C",
		        resolved_bundle.string(),
		        ".",
		});

		fs::create_directories(resolved_output.parent_path());
		copy_file_contents(resolved_launcher, resolved_output, false);
		copy_file_contents(archive_path, resolved_output, true);
		append_footer(resolved_output, Footer{.archive_size = file_size_u64(archive_path)});
		fs::permissions(resolved_output,
		                fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
		                fs::perm_options::add);
		fs::remove_all(temp_root);
	} catch (...) {
		fs::remove_all(temp_root);
		if (fs::exists(resolved_output)) {
			fs::remove(resolved_output);
		}
		throw;
	}
}

int run_embedded_bundle(const fs::path& executable_path,
                        const SelfExtractOptions& options)
{
	const auto resolved_executable = canonical_or_absolute(executable_path);
	const auto temp_root = create_temp_directory(options.extract_root);

	try {
		const auto bundle_path = extract_embedded_archive(resolved_executable, temp_root);
		const auto exit_code = launch_extracted_bundle(bundle_path, options);
		fs::remove_all(temp_root);
		return exit_code;
	} catch (...) {
		fs::remove_all(temp_root);
		throw;
	}
}

} // namespace dosbundle
