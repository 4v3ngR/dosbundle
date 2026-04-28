#pragma once

#include "bundle.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dosbundle {

struct DosboxVersion {
	std::string raw_output = {};
	int major = 0;
	int minor = 0;
	int patch = 0;
};

std::optional<fs::path> discover_dosbox_path();
DosboxVersion probe_dosbox_version(const fs::path& dosbox_path);
void validate_dosbox_version(const DosboxVersion& version);
std::vector<std::string> make_launch_args(const fs::path& dosbox_path,
                                          const BundleMetadata& bundle);
std::string format_command(const std::vector<std::string>& args);
int spawn_and_wait(const std::vector<std::string>& args);

} // namespace dosbundle
