//! Issue-link footers for DataZoo DuckDB extensions written in Rust.
//!
//! The anofox extensions are Rust crates behind a thin C++ shim. The load
//! banner is emitted from the C++ entry point (see `datazoo_banner.hpp`), so
//! this crate deliberately does *not* print anything — it exists only so that
//! errors raised on the Rust side already carry the issue link by the time they
//! cross the FFI boundary and become DuckDB exceptions.
//!
//! Wrap at the boundary, not at every `?`:
//!
//! ```
//! use datazoo_banner::BannerInfo;
//!
//! const BANNER: BannerInfo = BannerInfo::new(
//!     "anofox_bayes",
//!     env!("CARGO_PKG_VERSION"),
//!     "https://github.com/DataZooDE/anofox-bayes",
//! );
//!
//! fn to_duckdb_error(err: impl std::fmt::Display) -> String {
//!     BANNER.with_issue_hint(&err.to_string())
//! }
//! ```

#![forbid(unsafe_code)]

/// Identity of the extension raising the error. Mirrors the C++ `BannerInfo`
/// field for field so the two halves cannot drift.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BannerInfo {
    /// DuckDB extension name, e.g. `anofox_bayes`.
    pub extension: &'static str,
    /// Extension version, usually `env!("CARGO_PKG_VERSION")`.
    pub version: &'static str,
    /// Repository URL without a trailing slash.
    pub repo: &'static str,
}

impl BannerInfo {
    pub const fn new(extension: &'static str, version: &'static str, repo: &'static str) -> Self {
        Self { extension, version, repo }
    }

    pub fn issues_url(&self) -> String {
        format!("{}/issues", self.repo.trim_end_matches('/'))
    }

    /// The footer on its own line, matching the C++ `IssueHint` byte for byte.
    pub fn issue_hint(&self) -> String {
        format!("\n-> Unexpected? Please report it: {}", self.issues_url())
    }

    /// Appends the footer unless the message already carries an issue link.
    ///
    /// Idempotent by the same rule as the C++ side: an error annotated deep in
    /// the crate and then annotated again at the FFI boundary gets one footer,
    /// not two.
    pub fn with_issue_hint(&self, message: &str) -> String {
        if message.contains(&self.issues_url()) {
            return message.to_string();
        }
        format!("{}{}", message, self.issue_hint())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const BANNER: BannerInfo = BannerInfo::new(
        "anofox_bayes",
        "0.1.0",
        "https://github.com/DataZooDE/anofox-bayes",
    );

    #[test]
    fn builds_the_issues_url() {
        assert_eq!(
            BANNER.issues_url(),
            "https://github.com/DataZooDE/anofox-bayes/issues"
        );
    }

    #[test]
    fn tolerates_a_trailing_slash_in_the_repo_url() {
        let banner = BannerInfo::new("x", "1", "https://github.com/DataZooDE/x/");
        assert_eq!(banner.issues_url(), "https://github.com/DataZooDE/x/issues");
    }

    #[test]
    fn appends_the_hint_once() {
        let once = BANNER.with_issue_hint("posterior did not converge");
        let twice = BANNER.with_issue_hint(&once);
        assert_eq!(once, twice);
        assert!(once.starts_with("posterior did not converge"));
        assert!(once.ends_with("/anofox-bayes/issues"));
    }

    #[test]
    fn leaves_messages_that_already_link_the_tracker_alone() {
        let message = "see https://github.com/DataZooDE/anofox-bayes/issues/7";
        assert_eq!(BANNER.with_issue_hint(message), message);
    }

    /// The two implementations are copy-pasted prose; if either side is edited
    /// this is the test that should fail first.
    #[test]
    fn hint_matches_the_cpp_wording() {
        assert_eq!(
            BANNER.issue_hint(),
            "\n-> Unexpected? Please report it: https://github.com/DataZooDE/anofox-bayes/issues"
        );
    }
}
