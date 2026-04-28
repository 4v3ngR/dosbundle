#include "manifest.h"

#include "common.h"

#include <fstream>
#include <map>
#include <stdexcept>

namespace dosbundle {

namespace {

std::string parse_toml_string(const std::string& value, const std::string& context)
{
	if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
		throw std::runtime_error("expected quoted string for " + context);
	}

	std::string parsed = {};
	parsed.reserve(value.size() - 2);

	for (size_t i = 1; i + 1 < value.size(); ++i) {
		const char ch = value[i];
		if (ch == '\\') {
			if (i + 1 >= value.size() - 1) {
				throw std::runtime_error("unterminated escape in " + context);
			}
			const char escaped = value[++i];
			switch (escaped) {
			case '\\': parsed.push_back('\\'); break;
			case '"': parsed.push_back('"'); break;
			case 'n': parsed.push_back('\n'); break;
			case 'r': parsed.push_back('\r'); break;
			case 't': parsed.push_back('\t'); break;
			default:
				throw std::runtime_error("unsupported escape sequence in " + context);
			}
			continue;
		}
		parsed.push_back(ch);
	}
	return parsed;
}

fs::path resolve_manifest_path(const fs::path& manifest_dir, const std::string& input_path)
{
	const fs::path path_value = input_path;
	if (path_value.is_absolute()) {
		return path_value.lexically_normal();
	}
	return (manifest_dir / path_value).lexically_normal();
}

} // namespace

Manifest parse_manifest_file(const fs::path& manifest_path)
{
	std::ifstream input(manifest_path);
	if (!input) {
		throw std::runtime_error("unable to open manifest: " + manifest_path.string());
	}

	std::map<std::string, std::string> entries = {};
	std::string current_section = {};
	std::string line = {};
	size_t line_number = 0;

	while (std::getline(input, line)) {
		++line_number;
		const auto stripped = trim(line);
		if (stripped.empty() || starts_with(stripped, "#")) {
			continue;
		}

		if (stripped.front() == '[') {
			if (stripped.back() != ']') {
				throw std::runtime_error("line " + std::to_string(line_number) +
				                         ": malformed section header");
			}
			current_section = trim(stripped.substr(1, stripped.size() - 2));
			if (current_section.empty() || current_section.find('.') != std::string::npos) {
				throw std::runtime_error("line " + std::to_string(line_number) +
				                         ": only one-level sections are supported");
			}
			continue;
		}

		const auto equals = stripped.find('=');
		if (equals == std::string::npos) {
			throw std::runtime_error("line " + std::to_string(line_number) +
			                         ": expected key = value");
		}

		const auto key = trim(stripped.substr(0, equals));
		const auto raw_value = trim(stripped.substr(equals + 1));
		if (key.empty()) {
			throw std::runtime_error("line " + std::to_string(line_number) + ": empty key");
		}

		const auto full_key = current_section.empty() ? key : current_section + "." + key;
		if (entries.contains(full_key)) {
			throw std::runtime_error("line " + std::to_string(line_number) +
			                         ": duplicate key '" + full_key + "'");
		}
		entries.emplace(full_key,
		                parse_toml_string(raw_value,
		                                  "key '" + full_key + "' on line " +
		                                          std::to_string(line_number)));
	}

	const auto require = [&](const std::string& key) -> std::string {
		const auto it = entries.find(key);
		if (it == entries.end() || it->second.empty()) {
			throw std::runtime_error("missing required manifest key '" + key + "'");
		}
		return it->second;
	};

	return Manifest{
	        .name                = require("name"),
	        .version             = require("version"),
	        .target_platform     = require("target_platform"),
	        .payload_c_drive_dir = require("payload.c_drive_dir"),
	        .payload_startup     = require("payload.startup"),
	        .dosbox_config       = require("dosbox.config"),
	        .output_path         = require("output.path"),
	};
}

ResolvedManifest resolve_manifest(const fs::path& manifest_path)
{
	const auto absolute_manifest = fs::absolute(manifest_path).lexically_normal();
	const auto manifest_dir = absolute_manifest.parent_path();
	const auto manifest = parse_manifest_file(absolute_manifest);

	const auto c_drive_dir = resolve_manifest_path(manifest_dir, manifest.payload_c_drive_dir);
	const auto startup_path =
	        (c_drive_dir / fs::path(manifest.payload_startup)).lexically_normal();
	const auto dosbox_config = resolve_manifest_path(manifest_dir, manifest.dosbox_config);
	const auto output_path = resolve_manifest_path(manifest_dir, manifest.output_path);

	if (!fs::exists(c_drive_dir) || !fs::is_directory(c_drive_dir)) {
		throw std::runtime_error("payload.c_drive_dir does not exist or is not a directory: " +
		                         c_drive_dir.string());
	}
	if (!fs::exists(startup_path) || !fs::is_regular_file(startup_path)) {
		throw std::runtime_error("payload.startup does not exist inside C drive: " +
		                         startup_path.string());
	}
	if (!fs::exists(dosbox_config) || !fs::is_regular_file(dosbox_config)) {
		throw std::runtime_error("dosbox.config does not exist: " + dosbox_config.string());
	}

	return ResolvedManifest{
	        .raw           = manifest,
	        .manifest_path = absolute_manifest,
	        .manifest_dir  = manifest_dir,
	        .c_drive_dir   = c_drive_dir,
	        .startup_path  = startup_path,
	        .dosbox_config = dosbox_config,
	        .output_path   = output_path,
	};
}

} // namespace dosbundle
