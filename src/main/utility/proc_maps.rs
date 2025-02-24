use std::error::Error;
use std::fmt::Display;
use std::path::PathBuf;
use std::str::FromStr;

use once_cell::sync::Lazy;
use regex::Regex;

/// Whether a region of memory is shared.
#[derive(PartialEq, Eq, Debug, Copy, Clone)]
pub enum Sharing {
    Private,
    Shared,
}

impl FromStr for Sharing {
    type Err = Box<dyn Error>;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s == "p" {
            Ok(Sharing::Private)
        } else if s == "s" {
            Ok(Sharing::Shared)
        } else {
            Err(format!("Bad sharing specifier {}", s).into())
        }
    }
}

/// The "path" of where a region is mapped from.
#[derive(PartialEq, Eq, Debug, Clone)]
pub enum MappingPath {
    InitialStack,
    ThreadStack(i32),
    Vdso,
    Heap,
    OtherSpecial(String),
    Path(PathBuf),
}

impl FromStr for MappingPath {
    type Err = Box<dyn Error>;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        static SPECIAL_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"^\[(\S+)\]$").unwrap());
        static THREAD_STACK_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"^stack:(\d+)$").unwrap());

        if s.starts_with('/') {
            return Ok(MappingPath::Path(PathBuf::from(s)));
        }
        if let Some(caps) = SPECIAL_RE.captures(s) {
            let s = caps.get(1).unwrap().as_str();
            if let Some(caps) = THREAD_STACK_RE.captures(s) {
                return Ok(MappingPath::ThreadStack(
                    caps.get(1)
                        .unwrap()
                        .as_str()
                        .parse::<i32>()
                        .map_err(|e| format!("Parsing thread id: {}", e))?,
                ));
            }
            return Ok(match s {
                "stack" => MappingPath::InitialStack,
                "vdso" => MappingPath::Vdso,
                "heap" => MappingPath::Heap,
                // Experimentally there other labels here that are undocumented in proc(5). Some
                // examples include `vvar` and `vsyscall`. Rather than failing to parse, we just
                // save the label.
                _ => MappingPath::OtherSpecial(s.to_string()),
            });
        }
        Err(format!("Couldn't parse '{}'", s).into())
    }
}

/// Represents a single line in /proc/\[pid\]/maps.
#[derive(PartialEq, Eq, Debug, Clone)]
pub struct Mapping {
    pub begin: usize,
    pub end: usize,
    pub read: bool,
    pub write: bool,
    pub execute: bool,
    pub sharing: Sharing,
    pub offset: usize,
    pub device_major: i32,
    pub device_minor: i32,
    pub inode: u64,
    pub path: Option<MappingPath>,
    pub deleted: bool,
}

// Parses the given field with the given function, decorating errors with the field name and value.
fn parse_field<T, F, U>(field: &str, field_name: &str, parse_fn: F) -> Result<T, String>
where
    F: FnOnce(&str) -> Result<T, U>,
    U: Display,
{
    match parse_fn(field) {
        Ok(res) => Ok(res),
        Err(err) => Err(format!("Parsing {} '{}': {}", field_name, field, err)),
    }
}

impl FromStr for Mapping {
    type Err = Box<dyn Error>;
    fn from_str(line: &str) -> Result<Self, Self::Err> {
        static RE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(
                r"^(\S+)-(\S+)\s+(\S)(\S)(\S)(\S)\s+(\S+)\s+(\S+):(\S+)\s+(\S+)\s*(\S*)\s*(\S*)$",
            )
            .unwrap()
        });

        let caps = RE
            .captures(line)
            .ok_or_else(|| format!("Didn't match regex: {}", line))?;

        Ok(Mapping {
            begin: parse_field(caps.get(1).unwrap().as_str(), "begin", |s| {
                usize::from_str_radix(s, 16)
            })?,
            end: parse_field(caps.get(2).unwrap().as_str(), "end", |s| {
                usize::from_str_radix(s, 16)
            })?,
            read: {
                let s = caps.get(3).unwrap().as_str();
                match s {
                    "r" => true,
                    "-" => false,
                    _ => return Err(format!("Couldn't parse read bit {}", s).into()),
                }
            },
            write: {
                let s = caps.get(4).unwrap().as_str();
                match s {
                    "w" => true,
                    "-" => false,
                    _ => return Err(format!("Couldn't parse write bit {}", s).into()),
                }
            },
            execute: {
                let s = caps.get(5).unwrap().as_str();
                match s {
                    "x" => true,
                    "-" => false,
                    _ => return Err(format!("Couldn't parse execute bit {}", s).into()),
                }
            },
            sharing: caps.get(6).unwrap().as_str().parse::<Sharing>()?,
            offset: parse_field(caps.get(7).unwrap().as_str(), "offset", |s| {
                usize::from_str_radix(s, 16)
            })?,
            device_major: parse_field(caps.get(8).unwrap().as_str(), "device_major", |s| {
                i32::from_str_radix(s, 16)
            })?,
            device_minor: parse_field(caps.get(9).unwrap().as_str(), "device_minor", |s| {
                i32::from_str_radix(s, 16)
            })?,
            // Undocumented whether this is actually base 10; change to 16 if we find
            // counter-examples.
            inode: parse_field(caps.get(10).unwrap().as_str(), "inode", |s| s.parse())?,
            path: parse_field::<_, _, Box<dyn Error>>(
                caps.get(11).unwrap().as_str(),
                "path",
                |s| match s {
                    "" => Ok(None),
                    s => Ok(Some(s.parse::<MappingPath>()?)),
                },
            )?,
            deleted: {
                let s = caps.get(12).unwrap().as_str();
                match s {
                    "" => false,
                    "(deleted)" => true,
                    _ => return Err(format!("Couldn't parse trailing field '{}'", s).into()),
                }
            },
        })
    }
}

/// Parses the contents of a /proc/\[pid\]/maps file
pub fn parse_file_contents(mappings: &str) -> Result<Vec<Mapping>, Box<dyn Error>> {
    let res: Result<Vec<_>, String> = mappings
        .lines()
        .map(|line| Mapping::from_str(line).map_err(|e| format!("Parsing line: {}\n{}", line, e)))
        .collect();
    Ok(res?)
}

/// Reads and parses the contents of a /proc/\[pid\]/maps file
pub fn mappings_for_pid(pid: libc::pid_t) -> Result<Vec<Mapping>, Box<dyn Error>> {
    use std::fs::File;
    use std::io::Read;

    let mut file = File::open(format!("/proc/{}/maps", pid))?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)?;
    parse_file_contents(&contents)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_mapping_from_str() {
        // Private mapping of part of a file. Taken from proc(5).
        assert_eq!(
            "00400000-00452000 r-xp 00000000 08:02 173521      /usr/bin/dbus-daemon"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0x00400000,
                end: 0x00452000,
                read: true,
                write: false,
                execute: true,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 8,
                device_minor: 2,
                inode: 173521,
                path: Some(MappingPath::Path(PathBuf::from("/usr/bin/dbus-daemon"))),
                deleted: false,
            }
        );

        // Heap. Taken from proc(5).
        assert_eq!(
            "00e03000-00e24000 rw-p 00000000 00:00 0           [heap]"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0x00e03000,
                end: 0x00e24000,
                read: true,
                write: true,
                execute: false,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 0,
                device_minor: 0,
                inode: 0,
                path: Some(MappingPath::Heap),
                deleted: false,
            }
        );

        // Anonymous. Taken from proc(5).
        assert_eq!(
            "35b1a21000-35b1a22000 rw-p 00000000 00:00 0"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0x35b1a21000,
                end: 0x35b1a22000,
                read: true,
                write: true,
                execute: false,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 0,
                device_minor: 0,
                inode: 0,
                path: None,
                deleted: false,
            }
        );

        // Thread stack. Taken from proc(5).
        assert_eq!(
            "f2c6ff8c000-7f2c7078c000 rw-p 00000000 00:00 0    [stack:986]"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0xf2c6ff8c000,
                end: 0x7f2c7078c000,
                read: true,
                write: true,
                execute: false,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 0,
                device_minor: 0,
                inode: 0,
                path: Some(MappingPath::ThreadStack(986)),
                deleted: false,
            }
        );

        // Initial stack. Taken from proc(5).
        assert_eq!(
            "7fffb2c0d000-7fffb2c2e000 rw-p 00000000 00:00 0   [stack]"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0x7fffb2c0d000,
                end: 0x7fffb2c2e000,
                read: true,
                write: true,
                execute: false,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 0,
                device_minor: 0,
                inode: 0,
                path: Some(MappingPath::InitialStack),
                deleted: false,
            }
        );

        // vdso. Taken from proc(5).
        assert_eq!(
            "7fffb2d48000-7fffb2d49000 r-xp 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0x7fffb2d48000,
                end: 0x7fffb2d49000,
                read: true,
                write: false,
                execute: true,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 0,
                device_minor: 0,
                inode: 0,
                path: Some(MappingPath::Vdso),
                deleted: false,
            }
        );

        // vsyscall. Undocumented in proc(5), but found experimentally.
        assert_eq!(
            "7fffb2d48000-7fffb2d49000 r-xp 00000000 00:00 0   [vsyscall]"
                .parse::<Mapping>()
                .unwrap()
                .path,
            Some(MappingPath::OtherSpecial("vsyscall".to_string()))
        );

        // Hexadecimal device major and minor numbers. Base is unspecified in proc(5), but
        // experimentally they're hex.
        {
            let mapping = "00400000-00452000 r-xp 00000000 bb:cc 173521      /usr/bin/dbus-daemon"
                .parse::<Mapping>()
                .unwrap();
            assert_eq!(mapping.device_major, 0xbb);
            assert_eq!(mapping.device_minor, 0xcc);
        }

        // Undocumented '(deleted)' trailer, indicating that the mapped file has been removed from
        // the file system.
        {
            let mapping =
                "00400000-00452000 r-xp 00000000 bb:cc 173521      /usr/bin/dbus-daemon (deleted)"
                    .parse::<Mapping>()
                    .unwrap();
            assert!(mapping.deleted);
        }

        // A large 64-bit inode value.
        assert_eq!(
            "00400000-00452000 r-xp 00000000 08:02 18446744073709551615      /usr/bin/dbus-daemon"
                .parse::<Mapping>()
                .unwrap(),
            Mapping {
                begin: 0x00400000,
                end: 0x00452000,
                read: true,
                write: false,
                execute: true,
                sharing: Sharing::Private,
                offset: 0,
                device_major: 8,
                device_minor: 2,
                inode: 18446744073709551615,
                path: Some(MappingPath::Path(PathBuf::from("/usr/bin/dbus-daemon"))),
                deleted: false,
            }
        );
    }

    #[test]
    fn test_bad_inputs() {
        // These should return errors but *not* panic.

        // Empty
        assert!("".parse::<Mapping>().is_err());

        // Non-match
        assert!("garbage".parse::<Mapping>().is_err());

        // Validate parseable template as baseline, before testing different mutations of it below.
        "7fffb2d48000-7fffb2d49000 ---p 00000000 00:00 0   [vdso]"
            .parse::<Mapping>()
            .unwrap();

        // Bad begin
        assert!(
            "7fffb2d4800q-7fffb2d49000 ---p 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad end
        assert!(
            "7fffb2d48000-7fffb2d4900q ---p 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad r
        assert!(
            "7fffb2d48000-7fffb2d49000 z--p 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad w
        assert!(
            "7fffb2d48000-7fffb2d49000 -z-p 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad x
        assert!(
            "7fffb2d48000-7fffb2d49000 --zp 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad sharing
        assert!(
            "7fffb2d48000-7fffb2d49000 ---- 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad offset
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 0000000z 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad device high
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 00000000 0z:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad device low
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 00000000 00:0z 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad inode
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 00000000 00:00 z   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Bad path
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 00000000 00:00 0   z"
                .parse::<Mapping>()
                .is_err()
        );

        // Leading garbage
        assert!(
            "z 7fffb2d48000-7fffb2d49000 ---p 00000000 00:00 0   [vdso]"
                .parse::<Mapping>()
                .is_err()
        );

        // Trailing garbage after path
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 00000000 00:00 0   [vdso] z"
                .parse::<Mapping>()
                .is_err()
        );

        // Trailing garbage after (deleted)
        assert!(
            "7fffb2d48000-7fffb2d49000 ---p 00000000 00:00 0   [vdso] (deleted) z"
                .parse::<Mapping>()
                .is_err()
        );
    }

    #[test]
    fn test_parse_file_contents() {
        // Multiple lines.
        #[rustfmt::skip]
        let s = "00400000-00452000 r-xp 00000000 08:02 173521      /usr/bin/dbus-daemon\n\
                 7fffb2c0d000-7fffb2c2e000 rw-p 00000000 00:00 0   [stack]\n";
        assert_eq!(
            parse_file_contents(s).unwrap(),
            vec![
                Mapping {
                    begin: 0x00400000,
                    end: 0x00452000,
                    read: true,
                    write: false,
                    execute: true,
                    sharing: Sharing::Private,
                    offset: 0,
                    device_major: 8,
                    device_minor: 2,
                    inode: 173521,
                    path: Some(MappingPath::Path(PathBuf::from("/usr/bin/dbus-daemon"))),
                    deleted: false,
                },
                Mapping {
                    begin: 0x7fffb2c0d000,
                    end: 0x7fffb2c2e000,
                    read: true,
                    write: true,
                    execute: false,
                    sharing: Sharing::Private,
                    offset: 0,
                    device_major: 0,
                    device_minor: 0,
                    inode: 0,
                    path: Some(MappingPath::InitialStack),
                    deleted: false,
                }
            ]
        );

        // Empty.
        assert_eq!(parse_file_contents("").unwrap(), Vec::<Mapping>::new());
    }

    #[test]
    // Hangs in miri. Not sure why, but also not surpring in general that this
    // test would be incompatible.
    #[cfg_attr(miri, ignore)]
    fn test_mappings_for_pid() {
        // Difficult to write a precise test here; just try to read our own mappings and validate
        // that it parses and is non-empty.
        let pid = unsafe { libc::getpid() };
        let mappings = mappings_for_pid(pid).unwrap();
        assert!(!mappings.is_empty());
    }
}
