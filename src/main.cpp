#include "bundle.h"
#include "common.h"
#include "config.h"
#include "launch.h"
#include "manifest.h"
#include "package.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using dosbundle::discover_dosbox_path;
using dosbundle::format_command;
using dosbundle::load_bundle_metadata;
using dosbundle::make_launch_args;
using dosbundle::probe_dosbox_version;
using dosbundle::create_self_extracting_bundle;
using dosbundle::resolve_manifest;
using dosbundle::resolve_executable_path;
using dosbundle::run_embedded_bundle;
using dosbundle::spawn_and_wait;
using dosbundle::stage_bundle;
using dosbundle::validate_dosbox_version;
using dosbundle::executable_has_embedded_bundle;
constexpr std::string_view program_name = "dosbundle";

dosbundle::SelfExtractOptions parse_self_extract_options(int argc, char** argv)
{
	dosbundle::SelfExtractOptions options = {};

	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		if (arg == "--dry-run") {
			options.dry_run = true;
			continue;
		}
		if (arg == "--dosbox") {
			if (i + 1 >= argc) {
				throw std::runtime_error("missing path after --dosbox");
			}
			options.dosbox_override = fs::absolute(argv[++i]).lexically_normal();
			continue;
		}
		if (arg == "--extract-root") {
			if (i + 1 >= argc) {
				throw std::runtime_error("missing path after --extract-root");
			}
			options.extract_root = fs::absolute(argv[++i]).lexically_normal();
			continue;
		}
		throw std::runtime_error("unknown self-extract option '" + std::string(arg) + "'");
	}

	return options;
}

void print_usage()
{
	const auto config_path = dosbundle::app_config_path();
	std::cout
	        << program_name << " - DOS application bundler\n\n"
	        << "Usage:\n"
	        << "  " << program_name << " --help\n"
	        << "  " << program_name << " bundle <manifest> [-f|--force]\n"
	        << "  " << program_name
	        << " launch <bundle-dir> [--dosbox <path>] [--extract-root <path>] [--dry-run]\n"
	        << "  " << program_name
	        << " package <bundle-dir> <output> [--launcher <path>]\n\n"
	        << "Config:\n"
	        << "  " << config_path
	        << "  keys: dosbox_path=..., tar_path=..., extract_root=...\n\n"
	        << "Current status:\n"
	        << "  Stages, packages, and launches bundles using an external DOSBox Staging.\n";
}

} // namespace

int main(int argc, char** argv)
{
	const auto self_path = resolve_executable_path(argv[0]);
	if (executable_has_embedded_bundle(self_path)) {
		const bool explicit_command =
		        argc > 1 &&
		        (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h" ||
		         std::string_view(argv[1]) == "bundle" || std::string_view(argv[1]) == "launch" ||
		         std::string_view(argv[1]) == "package");
		if (!explicit_command) {
			try {
				return run_embedded_bundle(self_path, parse_self_extract_options(argc, argv));
			} catch (const std::exception& e) {
				std::cerr << "error: " << e.what() << '\n';
				return 2;
			}
		}
	}

	if (argc <= 1) {
		print_usage();
		return 1;
	}

	const std::string_view command = argv[1];

	if (command == "--help" || command == "-h") {
		print_usage();
		return 0;
	}

	if (command == "bundle") {
		if (argc < 3) {
			std::cerr << "error: missing manifest path\n";
			return 2;
		}

		bool force_overwrite = false;

		for (int i = 3; i < argc; ++i) {
			const std::string_view arg = argv[i];
			if (arg == "-f" || arg == "--force") {
				force_overwrite = true;
				continue;
			}
			std::cerr << "error: unknown bundle option '" << arg << "'\n";
			return 2;
		}

		try {
			const auto manifest = resolve_manifest(argv[2]);
			stage_bundle(manifest, force_overwrite);

			std::cout << "bundle staged successfully\n";
			std::cout << "output: " << manifest.output_path << '\n';
			std::cout << "startup: " << manifest.raw.payload_startup << '\n';
			return 0;
		} catch (const std::exception& e) {
			std::cerr << "error: " << e.what() << '\n';
			return 2;
		}
	}

	if (command == "launch") {
		if (argc < 3) {
			std::cerr << "error: missing bundle path\n";
			return 2;
		}

		std::optional<fs::path> dosbox_override = std::nullopt;
		std::optional<fs::path> extract_root = std::nullopt;
		bool dry_run = false;

		for (int i = 3; i < argc; ++i) {
			const std::string_view arg = argv[i];
			if (arg == "--dry-run") {
				dry_run = true;
				continue;
			}
			if (arg == "--dosbox") {
				if (i + 1 >= argc) {
					std::cerr << "error: missing path after --dosbox\n";
					return 2;
				}
				dosbox_override = fs::absolute(argv[++i]).lexically_normal();
				continue;
			}
			if (arg == "--extract-root") {
				if (i + 1 >= argc) {
					std::cerr << "error: missing path after --extract-root\n";
					return 2;
				}
				extract_root = fs::absolute(argv[++i]).lexically_normal();
				continue;
			}
			std::cerr << "error: unknown launch option '" << arg << "'\n";
			return 2;
		}

		try {
			const auto bundle = load_bundle_metadata(argv[2]);
			const auto dosbox_path = dosbox_override ? *dosbox_override : [&]() -> fs::path {
				const auto discovered = discover_dosbox_path();
				if (!discovered) {
					throw std::runtime_error(
					        "unable to find dosbox-staging; use --dosbox <path> or install it");
				}
				return *discovered;
			}();

			if (!fs::exists(dosbox_path) || !fs::is_regular_file(dosbox_path)) {
				throw std::runtime_error("dosbox executable does not exist: " +
				                         dosbox_path.string());
			}

			const auto version = probe_dosbox_version(dosbox_path);
			validate_dosbox_version(version);
			const auto launch_args = make_launch_args(dosbox_path, bundle);

			std::cout << "dosbox: " << dosbox_path << '\n';
			std::cout << "version: " << version.raw_output << '\n';
			if (extract_root) {
				std::cout << "extract-root: " << *extract_root << '\n';
			}
			std::cout << "command: " << format_command(launch_args) << '\n';

			if (dry_run) {
				return 0;
			}

			return spawn_and_wait(launch_args);
		} catch (const std::exception& e) {
			std::cerr << "error: " << e.what() << '\n';
			return 2;
		}
	}

	if (command == "package") {
		if (argc < 4) {
			std::cerr << "error: missing bundle path or output path\n";
			return 2;
		}

		fs::path launcher_path = self_path;

		for (int i = 4; i < argc; ++i) {
			const std::string_view arg = argv[i];
			if (arg == "--launcher") {
				if (i + 1 >= argc) {
					std::cerr << "error: missing path after --launcher\n";
					return 2;
				}
				launcher_path = fs::absolute(argv[++i]).lexically_normal();
				continue;
			}
			std::cerr << "error: unknown package option '" << arg << "'\n";
			return 2;
		}

		try {
			create_self_extracting_bundle(launcher_path, argv[2], argv[3]);
			std::cout << "self-extracting bundle created\n";
			std::cout << "launcher: " << launcher_path << '\n';
			std::cout << "bundle: " << fs::absolute(argv[2]).lexically_normal() << '\n';
			std::cout << "output: " << fs::absolute(argv[3]).lexically_normal() << '\n';
			return 0;
		} catch (const std::exception& e) {
			std::cerr << "error: " << e.what() << '\n';
			return 2;
		}
	}

	std::cerr << "error: unknown command '" << command << "'\n";
	print_usage();
	return 2;
}
