// Minimal stand-in for posthog-telemetry, used only to compile the
// DATAZOO_BANNER_TELEMETRY code path.
//
// The real library lives in a separate repo and drags in OpenSSL and DuckDB
// headers, so this repo's CI cannot build against it. Without this stub the
// telemetry branch was compiled by nobody here and by every consumer -- which
// is exactly how an unqualified PostHogTelemetry reference shipped and broke
// the first extension build that switched the flag on.
//
// It reproduces only what ReportBannerShown touches, including the detail that
// caused that bug: the class lives inside namespace duckdb.

#pragma once

#include <map>
#include <string>

namespace duckdb {

struct PropertyValue {
	std::string value;
	PropertyValue(std::string v) : value(std::move(v)) {}
};

using PropertyMap = std::map<std::string, PropertyValue>;

class PostHogTelemetry {
public:
	static PostHogTelemetry &Instance() {
		static PostHogTelemetry instance;
		return instance;
	}
	bool IsEnabled() const {
		return enabled;
	}
	void SetEnabled(bool value) {
		enabled = value;
	}
	void Capture(const std::string &event, PropertyMap properties = {}) {
		last_event = event;
		last_properties = std::move(properties);
	}

	std::string last_event;
	PropertyMap last_properties;

private:
	bool enabled = true;
};

} // namespace duckdb
