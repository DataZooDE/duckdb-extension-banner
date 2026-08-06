// Standalone tests for the gating logic. No DuckDB, no test framework: the
// header is dependency-free and the tests should stay that way, so this is a
// plain binary that returns non-zero on failure.
//
// The gating is the part that must never regress — a banner that leaks into a
// pipe or a CI log is the one failure mode that would force us to revert the
// whole rollout.

#include "datazoo_banner.hpp"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>

// The header is portable; this test's fd juggling and env manipulation are not,
// so the POSIX spellings are mapped rather than the test being skipped on
// Windows — the gating rules are exactly what we want covered there too.
#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define open _open
#define lseek _lseek
using off_t = long;
static int setenv(const char *name, const char *value, int) {
	return _putenv_s(name, value);
}
static int unsetenv(const char *name) {
	return _putenv_s(name, "");
}
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

void Check(bool condition, const char *what) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void ClearEnvironment() {
	static const char *const vars[] = {"DATAZOO_NO_BANNER", "NO_BANNER",  "CI",
	                                   "GITHUB_ACTIONS",    "GITLAB_CI",  "BUILDKITE",
	                                   "JENKINS_URL",       "TEAMCITY_VERSION",
	                                   "DUCKDB_TEST_RUNNER", "DATAZOO_TEST"};
	for (const char *var : vars) {
		unsetenv(var);
	}
	datazoo::BannerEnabledFlag() = true;
}

constexpr datazoo::BannerInfo kInfo {"erpl_tunnel", "2026.07.24",
                                     "https://github.com/DataZooDE/erpl-tunnel"};

void TestUrls() {
	Check(datazoo::IssuesUrl(kInfo) == "https://github.com/DataZooDE/erpl-tunnel/issues",
	      "issues url is repo + /issues");
	Check(datazoo::IssueHint(kInfo).find("erpl-tunnel/issues") != std::string::npos,
	      "hint carries the issues url");
	Check(datazoo::IssueHint(kInfo)[0] == '\n', "hint starts on its own line");
}

void TestIssueHintIsIdempotent() {
	const std::string once = datazoo::WithIssueHint("boom", kInfo);
	const std::string twice = datazoo::WithIssueHint(once, kInfo);
	Check(once == twice, "hint is not appended twice by nested guards");
	Check(once.find("boom") == 0, "original message is preserved at the front");

	// A message that already mentions the repo for its own reasons should not
	// collect a second, redundant link.
	const std::string mentions = "see https://github.com/DataZooDE/erpl-tunnel/issues/12";
	Check(datazoo::WithIssueHint(mentions, kInfo) == mentions,
	      "existing issue reference suppresses the hint");
}

void TestEnvironmentGating() {
	ClearEnvironment();

	setenv("DATAZOO_NO_BANNER", "1", 1);
	Check(datazoo::BannerSuppressed(), "DATAZOO_NO_BANNER=1 suppresses");

	// Explicit off-values must not read as "suppress" — the variable is an
	// opt-out, so 0/false/no mean "leave the banner alone".
	setenv("DATAZOO_NO_BANNER", "0", 1);
	Check(!datazoo::banner_detail::EnvTruthy("DATAZOO_NO_BANNER"), "DATAZOO_NO_BANNER=0 is falsy");
	setenv("DATAZOO_NO_BANNER", "false", 1);
	Check(!datazoo::banner_detail::EnvTruthy("DATAZOO_NO_BANNER"),
	      "DATAZOO_NO_BANNER=false is falsy");
	unsetenv("DATAZOO_NO_BANNER");

	setenv("CI", "true", 1);
	Check(datazoo::BannerSuppressed(), "CI suppresses");
	unsetenv("CI");

	setenv("GITHUB_ACTIONS", "true", 1);
	Check(datazoo::BannerSuppressed(), "GITHUB_ACTIONS suppresses");
	unsetenv("GITHUB_ACTIONS");

	setenv("DUCKDB_TEST_RUNNER", "1", 1);
	Check(datazoo::BannerSuppressed(), "DuckDB test runner suppresses");
	unsetenv("DUCKDB_TEST_RUNNER");

	ClearEnvironment();
	datazoo::BannerEnabledFlag() = false;
	Check(datazoo::BannerSuppressed(), "SET datazoo_banner=false suppresses");
	datazoo::BannerEnabledFlag() = true;
}

// The decisive test: under a test harness, stderr is not a terminal, so a call
// must produce no bytes at all. This is what keeps 19 existing test suites
// unchanged.
void TestSilentWhenNotATty() {
	ClearEnvironment();
	std::fflush(stderr);

	// Windows has no /tmp, and an unopenable path here would leave the fd
	// juggling below operating on -1 rather than reporting a clean failure.
	std::string temp_dir;
	for (const char *candidate : {"TMPDIR", "TEMP", "TMP"}) {
		const char *value = std::getenv(candidate);
		if (value != nullptr && value[0] != '\0') {
			temp_dir = value;
			break;
		}
	}
	if (temp_dir.empty()) {
		temp_dir = "/tmp";
	}
	const std::string path = temp_dir + "/datazoo_banner_test_stderr";

	// dup the real stderr aside rather than freopen-ing it back: the test also
	// runs where /dev/tty cannot be opened, and losing stderr there would make
	// any subsequent failure invisible.
	const int saved = dup(fileno(stderr));
	const int sink = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (sink < 0 || saved < 0) {
		Check(false, "could not redirect stderr for the tty test");
		return;
	}
	dup2(sink, fileno(stderr));

	datazoo::ShowBanner(kInfo);
	datazoo::ShowBannerStandalone(kInfo);
	std::fflush(stderr);

	const off_t written = lseek(sink, 0, SEEK_END);
	dup2(saved, fileno(stderr));
	close(sink);
	close(saved);
	std::remove(path.c_str());

	Check(written == 0, "no output when stderr is not a tty");
}

void TestStampFile() {
	ClearEnvironment();
	const std::string stamp = datazoo::banner_detail::StampPath("banner_selftest");
	Check(!stamp.empty(), "stamp path resolves under HOME");
	std::remove(stamp.c_str());

	Check(!datazoo::banner_detail::StampIsFresh(stamp, 3600), "missing stamp is not fresh");
	datazoo::banner_detail::TouchStamp(stamp);
	Check(datazoo::banner_detail::StampIsFresh(stamp, 3600), "just-written stamp is fresh");
	Check(!datazoo::banner_detail::StampIsFresh(stamp, 0), "a zero ttl expires immediately");
	std::remove(stamp.c_str());
}

void TestBoxRendering() {
	const auto lines = datazoo::BannerLines(kInfo);
	bool has_issues = false;
	bool has_optout = false;
	for (const std::string &line : lines) {
		has_issues |= line.find("/issues") != std::string::npos;
		has_optout |= line.find("datazoo_banner=false") != std::string::npos;
	}
	Check(has_issues, "banner shows the issues url");
	Check(has_optout, "banner tells the user how to silence it");
	Check(datazoo::banner_detail::DisplayWidth("abc") == 3, "ascii width");
	Check(datazoo::banner_detail::DisplayWidth("\xE2\x98\x85") == 1, "star counts as one column");
}

} // namespace

int main() {
	TestUrls();
	TestIssueHintIsIdempotent();
	TestEnvironmentGating();
	TestStampFile();
	TestBoxRendering();
	TestSilentWhenNotATty();

	if (failures > 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("all banner checks passed\n");
	return 0;
}
