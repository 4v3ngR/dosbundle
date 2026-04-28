#pragma once

#include "bundle.h"

#include <filesystem>
#include <optional>
#include <string>

namespace dosbundle {

struct SelfExtractOptions {
	std::optional<fs::path> dosbox_override = std::nullopt;
	std::optional<fs::path> extract_root = std::nullopt;
	bool dry_run = false;
};

bool executable_has_embedded_bundle(const fs::path& executable_path);
void create_self_extracting_bundle(const fs::path& launcher_path,
                                   const fs::path& bundle_path,
                                   const fs::path& output_path);
int run_embedded_bundle(const fs::path& executable_path,
                        const SelfExtractOptions& options);

} // namespace dosbundle
