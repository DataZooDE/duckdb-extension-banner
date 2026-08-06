// datazoo_banner.hpp — a one-per-day, terminal-only feedback nudge for DataZoo
// DuckDB extensions and CLIs.
//
// Header-only and dependency-free on purpose: this header is included by
// loadable DuckDB extensions, by CLIs (erpl-adt), by servers (flapi) and by an
// RFC daemon (erpl-rev). Anything DuckDB-specific lives in
// datazoo_banner_duckdb.hpp, which is the only header that includes duckdb.hpp.
//
// The guiding rule is that this must never be noise. A banner prints only when
// a human is demonstrably watching a terminal, and then at most once a day per
// extension. Everything else — pipes, notebooks, CI, tests, servers under
// systemd — stays byte-for-byte silent, so no existing test suite changes.
//
// Opt out with DATAZOO_NO_BANNER=1 or, inside DuckDB, SET datazoo_banner=false.

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef DATAZOO_BANNER_TELEMETRY
#include "telemetry.hpp"
#endif

namespace datazoo {

// Identity of the thing showing the banner. Held by pointer, not copied: every
// call site passes string literals with static storage duration.
struct BannerInfo {
	const char *extension;  // "erpl_tunnel"
	const char *version;    // "2026.07.24"
	const char *repo;       // "https://github.com/DataZooDE/erpl-tunnel"
};

// Set to false by `SET datazoo_banner=false`. Process-wide: an extension loaded
// after the user disabled banners in the same session stays quiet too.
inline bool &BannerEnabledFlag() {
	static bool enabled = true;
	return enabled;
}

namespace banner_detail {

constexpr long kStampTtlSeconds = 24 * 60 * 60;

// Truthiness for opt-out variables: set, non-empty and not an explicit "off".
// So DATAZOO_NO_BANNER=0 does what a reader expects rather than the opposite.
inline bool EnvTruthy(const char *name) {
	const char *value = std::getenv(name);
	if (value == nullptr || value[0] == '\0') {
		return false;
	}
	return !(std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
	         std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "no") == 0);
}

inline bool EnvPresent(const char *name) {
	const char *value = std::getenv(name);
	return value != nullptr && value[0] != '\0';
}

inline bool IsStderrTty() {
#ifdef _WIN32
	return _isatty(_fileno(stderr)) != 0;
#else
	return isatty(fileno(stderr)) != 0;
#endif
}

inline bool IsStdoutTty() {
#ifdef _WIN32
	return _isatty(_fileno(stdout)) != 0;
#else
	return isatty(fileno(stdout)) != 0;
#endif
}

// CI, test harnesses and DuckDB's own unit tests must see no output at all,
// otherwise every extension's test suite would need a new expected-stderr.
inline bool IsAutomatedEnvironment() {
	static const char *const markers[] = {"CI",
	                                      "GITHUB_ACTIONS",
	                                      "GITLAB_CI",
	                                      "BUILDKITE",
	                                      "JENKINS_URL",
	                                      "TEAMCITY_VERSION",
	                                      "DUCKDB_TEST_RUNNER",
	                                      "DATAZOO_TEST"};
	for (const char *marker : markers) {
		if (EnvPresent(marker)) {
			return true;
		}
	}
	return false;
}

inline std::string HomeDirectory() {
#ifdef _WIN32
	const char *home = std::getenv("USERPROFILE");
	if (home == nullptr || home[0] == '\0') {
		home = std::getenv("APPDATA");
	}
#else
	const char *home = std::getenv("HOME");
#endif
	return home != nullptr ? std::string(home) : std::string();
}

inline bool MakeDirectory(const std::string &path) {
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
	return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// ~/.duckdb/datazoo_banner/<extension>. Empty string means "no writable home",
// in which case we degrade to once-per-process rather than failing.
inline std::string StampPath(const char *extension) {
	const std::string home = HomeDirectory();
	if (home.empty()) {
		return std::string();
	}
	const std::string duckdb_dir = home + "/.duckdb";
	const std::string stamp_dir = duckdb_dir + "/datazoo_banner";
	if (!MakeDirectory(duckdb_dir) || !MakeDirectory(stamp_dir)) {
		return std::string();
	}
	return stamp_dir + "/" + extension;
}

// True when the stamp exists and is younger than the TTL, i.e. we showed this
// extension's banner recently and should stay quiet.
inline bool StampIsFresh(const std::string &path, long ttl_seconds) {
	if (path.empty()) {
		return false;
	}
#ifdef _WIN32
	struct _stat64 info;
	if (_stat64(path.c_str(), &info) != 0) {
		return false;
	}
#else
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		return false;
	}
#endif
	const long age = static_cast<long>(std::time(nullptr) - info.st_mtime);
	return age >= 0 && age < ttl_seconds;
}

inline void TouchStamp(const std::string &path) {
	if (path.empty()) {
		return;
	}
	FILE *handle = std::fopen(path.c_str(), "w");
	if (handle != nullptr) {
		std::fputs("1\n", handle);
		std::fclose(handle);
	}
}

// Number of UTF-8 code points, used as a stand-in for display width. Our banner
// text is ASCII plus a single '*'-class symbol, so this is exact in practice
// and cheaper than pulling in a width table.
inline size_t DisplayWidth(const std::string &text) {
	size_t width = 0;
	for (unsigned char byte : text) {
		if ((byte & 0xC0) != 0x80) {
			width++;
		}
	}
	return width;
}

// Windows consoles are still frequently code-page 437/1252, where box-drawing
// characters and '★' render as mojibake. ASCII there, UTF-8 everywhere else.
inline bool UseUnicodeFrame() {
#ifdef _WIN32
	return false;
#else
	return true;
#endif
}

inline void RenderBox(const std::vector<std::string> &lines, FILE *out) {
	const bool unicode = UseUnicodeFrame();
	const char *tl = unicode ? "\xE2\x94\x8C" : "+";
	const char *tr = unicode ? "\xE2\x94\x90" : "+";
	const char *bl = unicode ? "\xE2\x94\x94" : "+";
	const char *br = unicode ? "\xE2\x94\x98" : "+";
	const char *h = unicode ? "\xE2\x94\x80" : "-";
	const char *v = unicode ? "\xE2\x94\x82" : "|";

	size_t width = 0;
	for (const std::string &line : lines) {
		width = DisplayWidth(line) > width ? DisplayWidth(line) : width;
	}

	std::string rule;
	for (size_t i = 0; i < width + 2; i++) {
		rule += h;
	}

	std::fprintf(out, "%s%s%s\n", tl, rule.c_str(), tr);
	for (const std::string &line : lines) {
		const size_t pad = width - DisplayWidth(line);
		std::fprintf(out, "%s %s%s %s\n", v, line.c_str(), std::string(pad, ' ').c_str(), v);
	}
	std::fprintf(out, "%s%s%s\n", bl, rule.c_str(), br);
	std::fflush(out);
}

// Fired only when a banner actually printed, so the event count is an
// impression count. Rides the extension's existing telemetry consent — if the
// user set erpl_telemetry_enabled=false, IsEnabled() is false and nothing is
// sent. Compiled out entirely unless the consumer defines the macro.
inline void ReportBannerShown(const BannerInfo &info, const char *surface) {
#ifdef DATAZOO_BANNER_TELEMETRY
	auto &telemetry = PostHogTelemetry::Instance();
	if (!telemetry.IsEnabled()) {
		return;
	}
	telemetry.Capture("banner_shown", {{"extension", std::string(info.extension)},
	                                   {"extension_version", std::string(info.version)},
	                                   {"repo", std::string(info.repo)},
	                                   {"surface", std::string(surface)}});
#else
	(void)info;
	(void)surface;
#endif
}

} // namespace banner_detail

inline std::string IssuesUrl(const BannerInfo &info) {
	return std::string(info.repo) + "/issues";
}

// Appended to error messages. Deliberately one line: it rides along with real
// diagnostics, and a multi-line CTA would bury the actual error.
inline std::string IssueHint(const BannerInfo &info) {
	return "\n-> Unexpected? Please report it: " + IssuesUrl(info);
}

// Idempotent: a message that already carries the hint (because an inner frame
// added it) is returned unchanged, so nested Guards don't stack footers.
inline std::string WithIssueHint(const std::string &message, const BannerInfo &info) {
	const std::string hint = IssueHint(info);
	if (message.size() >= hint.size() &&
	    message.compare(message.size() - hint.size(), hint.size(), hint) == 0) {
		return message;
	}
	if (message.find(IssuesUrl(info)) != std::string::npos) {
		return message;
	}
	return message + hint;
}

// Every gate that has nothing to do with the once-a-day stamp. Split out so the
// stamp is only touched by callers that really are about to print.
inline bool BannerSuppressed() {
	if (!BannerEnabledFlag()) {
		return true;
	}
	if (banner_detail::EnvTruthy("DATAZOO_NO_BANNER")) {
		return true;
	}
	if (banner_detail::EnvTruthy("NO_BANNER")) {
		return true;
	}
	if (banner_detail::IsAutomatedEnvironment()) {
		return true;
	}
	if (!banner_detail::IsStderrTty()) {
		return true;
	}
	return false;
}

inline std::vector<std::string> BannerLines(const BannerInfo &info) {
	const bool unicode = banner_detail::UseUnicodeFrame();
	const char *star = unicode ? "\xE2\x98\x85" : "*";

	std::vector<std::string> lines;
	lines.push_back(std::string(info.extension) + " " + info.version);
	lines.push_back("");
	lines.push_back("Hit a bug or something unexpected? Please tell us --");
	lines.push_back("every issue makes the next release better:");
	lines.push_back("  " + IssuesUrl(info));
	lines.push_back(std::string(star) + " If it saved you time, a star helps others find it:");
	lines.push_back("  " + std::string(info.repo));
	lines.push_back("");
	lines.push_back("(silence this: SET datazoo_banner=false, or DATAZOO_NO_BANNER=1)");
	return lines;
}

// The extension entry point. Safe to call from LoadInternal on any platform;
// never throws, never fails a load.
inline void ShowBanner(const BannerInfo &info) {
	if (BannerSuppressed()) {
		return;
	}
	const std::string stamp = banner_detail::StampPath(info.extension);
	if (banner_detail::StampIsFresh(stamp, banner_detail::kStampTtlSeconds)) {
		return;
	}
	// No writable stamp (read-only or absent HOME) degrades to once per process
	// instead of once per day, which is still bounded.
	if (stamp.empty()) {
		static bool shown_this_process = false;
		if (shown_this_process) {
			return;
		}
		shown_this_process = true;
	}
	banner_detail::RenderBox(BannerLines(info), stderr);
	banner_detail::TouchStamp(stamp);
	banner_detail::ReportBannerShown(info, "extension_load");
}

// The entry point for CLIs and servers, which have no DBConfig to hang a SET
// option on. `machine_readable` lets a CLI declare that this invocation is
// producing parseable output (--json, --quiet, --version) — those stay silent
// even on a TTY. stdout being redirected is treated the same way: a command in
// a pipeline should not decorate the terminal.
inline void ShowBannerStandalone(const BannerInfo &info, bool machine_readable = false) {
	if (machine_readable) {
		return;
	}
	if (!banner_detail::IsStdoutTty()) {
		return;
	}
	if (BannerSuppressed()) {
		return;
	}
	const std::string stamp = banner_detail::StampPath(info.extension);
	if (banner_detail::StampIsFresh(stamp, banner_detail::kStampTtlSeconds)) {
		return;
	}
	if (stamp.empty()) {
		static bool shown_this_process = false;
		if (shown_this_process) {
			return;
		}
		shown_this_process = true;
	}
	banner_detail::RenderBox(BannerLines(info), stderr);
	banner_detail::TouchStamp(stamp);
	banner_detail::ReportBannerShown(info, "process_start");
}

// For surfaces that are always visible and not rate-limited: `--help` output and
// the startup log line of a daemon nobody watches on a TTY.
inline std::string FeedbackLine(const BannerInfo &info) {
	return "Feedback, bugs and stars: " + std::string(info.repo);
}

} // namespace datazoo
