// Fails to compile (missing telemetry.hpp) or fails the static_assert when
// the banner's telemetry macro does not follow the posthog_telemetry target.
#include "datazoo_banner.hpp"

#ifdef DATAZOO_BANNER_TELEMETRY
static_assert(EXPECT_TELEMETRY == 1, "telemetry enabled without a posthog_telemetry target");
#else
static_assert(EXPECT_TELEMETRY == 0, "posthog_telemetry target present but telemetry compiled out");
#endif

int main() {
	return 0;
}
