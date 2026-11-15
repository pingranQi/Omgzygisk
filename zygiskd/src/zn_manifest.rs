//! Parse the public Zygisk Next `zn_modules.txt` manifest format.
//!
//! A row is: `name=<executable>` or `path=<absolute-path>`, optional
//! `companion` tokens, then the module library path as the final token.
//! This module is deliberately platform-independent so the matching rules are
//! covered by host tests.

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Target {
    Name(String),
    Path(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestEntry {
    pub target: Target,
    pub companion: bool,
    pub library: String,
}

pub fn parse_line(line: &str) -> Option<ManifestEntry> {
    let fields: Vec<&str> = line.split_whitespace().collect();
    if fields.len() < 2 {
        return None;
    }

    let target = if let Some(value) = fields[0].strip_prefix("name=") {
        if value.is_empty() { return None; }
        Target::Name(value.to_owned())
    } else if let Some(value) = fields[0].strip_prefix("path=") {
        if value.is_empty() { return None; }
        Target::Path(value.to_owned())
    } else {
        return None;
    };

    let library = fields.last()?.to_string();
    if library.is_empty() { return None; }
    let companion = fields[1..fields.len() - 1]
        .iter()
        .any(|field| *field == "companion");

    Some(ManifestEntry { target, companion, library })
}

impl ManifestEntry {
    pub fn matches(&self, process_name: &str, process_path: &str) -> bool {
        match &self.target {
            Target::Name(name)
                if matches!(name.as_str(), "zygote" | "zygote64" | "zygote32") =>
            {
                process_name.contains("zygote") || process_name.contains("app_process")
            }
            Target::Name(name) => process_name == name,
            Target::Path(path) => process_path == path,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_name_target_with_companion() {
        let entry = parse_line("name=com.example.server companion lib/arm64-v8a.so").unwrap();
        assert_eq!(entry.target, Target::Name("com.example.server".into()));
        assert!(entry.companion);
        assert_eq!(entry.library, "lib/arm64-v8a.so");
        assert!(entry.matches("com.example.server", "/system/bin/com.example.server"));
    }

    #[test]
    fn parses_exact_path_target() {
        let entry = parse_line("path=/system_ext/bin/hyos_spawner lib/hyos.so").unwrap();
        assert!(entry.matches("hyos_spawner", "/system_ext/bin/hyos_spawner"));
        assert!(!entry.matches("hyos_spawner", "/system/bin/hyos_spawner"));
    }

    #[test]
    fn zygote_target_covers_app_process_names() {
        let entry = parse_line("name=zygote companion zygisk.so").unwrap();
        assert!(entry.matches("zygote64", "/system/bin/app_process64"));
        assert!(entry.matches("app_process32", "/system/bin/app_process32"));
        assert!(!entry.matches("system_server", "/system/bin/system_server"));
    }

    #[test]
    fn zygote32_and_zygote64_targets_follow_zygote_matching() {
        for target in ["zygote32", "zygote64"] {
            let entry = parse_line(&format!("name={target} lib.so")).unwrap();
            assert!(entry.matches("zygote64", "/system/bin/app_process64"));
            assert!(entry.matches("zygote", "/system/bin/app_process32"));
            assert!(!entry.matches("system_server", "/system/bin/system_server"));
        }
    }

    #[test]
    fn rejects_malformed_rows() {
        assert!(parse_line("").is_none());
        assert!(parse_line("zygote lib.so").is_none());
        assert!(parse_line("name= lib.so").is_none());
    }
}
