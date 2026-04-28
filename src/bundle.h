#pragma once

#include "manifest.h"

#include <filesystem>
#include <string>

namespace dosbundle {

struct BundleMetadata {
	fs::path bundle_path = {};
	fs::path config_path = {};
	fs::path payload_root = {};
	fs::path startup_path = {};
	fs::path manifest_path = {};
	std::string startup_relative_path = {};
};

void stage_bundle(const ResolvedManifest& manifest, bool force_overwrite = false);
BundleMetadata load_bundle_metadata(const fs::path& bundle_path);

} // namespace dosbundle
