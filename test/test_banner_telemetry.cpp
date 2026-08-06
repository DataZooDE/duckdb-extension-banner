// Compiles and exercises the DATAZOO_BANNER_TELEMETRY path against the stub in
// telemetry_stub/. The gating tests cover behaviour; this one exists to make
// sure the telemetry branch still *compiles* and respects consent.

#include "datazoo_banner.hpp"

#include <cstdio>

namespace {

int failures = 0;

void Check(bool condition, const char *what) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

constexpr datazoo::BannerInfo kInfo {"erpl_tunnel", "2026.07.24",
                                     "https://github.com/DataZooDE/erpl-tunnel"};

} // namespace

int main() {
	auto &telemetry = duckdb::PostHogTelemetry::Instance();

	telemetry.Capture("");
	datazoo::banner_detail::ReportBannerShown(kInfo, "extension_load");
	Check(telemetry.last_event == "banner_shown", "a shown banner reports banner_shown");
	Check(telemetry.last_properties.at("extension").value == "erpl_tunnel",
	      "the event names the extension");
	Check(telemetry.last_properties.at("surface").value == "extension_load",
	      "the event names the surface");

	// Consent is the telemetry library's, not ours: if the user turned telemetry
	// off, a banner impression must not be reported either.
	telemetry.Capture("");
	telemetry.SetEnabled(false);
	datazoo::banner_detail::ReportBannerShown(kInfo, "extension_load");
	Check(telemetry.last_event.empty(), "disabled telemetry reports nothing");
	telemetry.SetEnabled(true);

	if (failures > 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("all telemetry checks passed\n");
	return 0;
}
