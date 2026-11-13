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
//!
//! Only errors that look like bugs should carry the footer. When the boundary
//! knows which DuckDB exception the shim will raise, pass its type name and a
//! user error (invalid input, a violated constraint, a missing file, ...) is
//! left alone:
//!
//! ```
//! # use datazoo_banner::BannerInfo;
//! # const BANNER: BannerInfo = BannerInfo::new("x", "1", "https://github.com/DataZooDE/x");
//! assert_eq!(BANNER.with_issue_hint_for("argument must not be NULL", "Invalid Input"),
//!            "argument must not be NULL");
//! assert!(BANNER.with_issue_hint_for("unreachable state", "INTERNAL").ends_with("/x/issues"));
//! ```

#![forbid(unsafe_code)]

/// DuckDB exception type names (as `Exception::ExceptionTypeToString` spells
/// them) that are the user's own mistake. Identical to the C++
/// `IsUserErrorType` list; `user_error_types_match_the_cpp_list` pins it.
const USER_ERROR_TYPES: &[&str] = &[
    "Invalid Input",
    "Constraint",
    "Binder",
    "Catalog",
    "Not implemented",
    "TransactionContext",
    "Conversion",
    "Out of Range",
    "Parser",
    "Permission",
    "IO",
    "HTTP",
    "Invalid Configuration",
    "Syntax",
];

/// True when `type_name` (a DuckDB exception type name, compared ASCII
/// case-insensitively) is a user error, which gets no issue footer. INTERNAL,
/// FATAL and unknown names are not user errors.
pub fn is_user_error_type(type_name: &str) -> bool {
    USER_ERROR_TYPES
        .iter()
        .any(|candidate| candidate.eq_ignore_ascii_case(type_name))
}

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

    /// [`with_issue_hint`](Self::with_issue_hint), but only for errors that
    /// look like bugs: a user error type (see [`is_user_error_type`]) comes
    /// back unchanged. Mirrors the C++ `WithIssueHintFor`.
    pub fn with_issue_hint_for(&self, message: &str, type_name: &str) -> String {
        if is_user_error_type(type_name) {
            return message.to_string();
        }
        self.with_issue_hint(message)
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

    #[test]
    fn user_errors_get_no_hint() {
        for type_name in USER_ERROR_TYPES {
            assert_eq!(BANNER.with_issue_hint_for("bad argument", type_name), "bad argument");
        }
        assert!(is_user_error_type("invalid input"));
    }

    #[test]
    fn unexpected_errors_get_the_hint() {
        for type_name in ["INTERNAL", "FATAL", "Invalid", "Unknown Type", "", "Invalid Inputs"] {
            assert!(!is_user_error_type(type_name), "{type_name}");
            assert_eq!(
                BANNER.with_issue_hint_for("boom", type_name),
                BANNER.with_issue_hint("boom")
            );
        }
    }

    #[test]
    fn with_issue_hint_for_is_idempotent() {
        let once = BANNER.with_issue_hint_for("boom", "INTERNAL");
        assert_eq!(BANNER.with_issue_hint_for(&once, "INTERNAL"), once);
    }

    /// Copy of the C++ list in datazoo_banner.hpp; edit both or neither.
    #[test]
    fn user_error_types_match_the_cpp_list() {
        let header = include_str!("../../../include/datazoo_banner.hpp");
        let start = header.find("kUserErrorTypes[] = {").expect("C++ list");
        let end = start + header[start..].find("};").unwrap();
        let mut cpp: Vec<&str> = header[start..end].split('"').skip(1).step_by(2).collect();
        let mut rust = USER_ERROR_TYPES.to_vec();
        cpp.sort_unstable();
        rust.sort_unstable();
        assert_eq!(cpp, rust);
    }
}
