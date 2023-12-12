use std::ffi::CStr;
use std::fs::File;
use std::io::Write;
use std::os::fd::AsRawFd;
use std::path::{Path, PathBuf};

use crate::core::configuration::ConfigOptions;

use anyhow::Context;
use nix::sys::memfd::MemFdCreateFlag;

#[cfg(debug_assertions)]
#[allow(unused_macros)]
macro_rules! target {
    () => {
        "../../../build/src/shim/target/debug/"
    };
}
#[cfg(not(debug_assertions))]
#[allow(unused_macros)]
macro_rules! target {
    () => {
        "../../../build/src/shim/target/release/"
    };
}

cfg_if::cfg_if! {
    if #[cfg(all(not(feature = "cargo-clippy"), not(miri), not(doctest), not(loom), not(doc)))] {
        const SHIM_LIB: &[u8] = std::include_bytes!(concat!(target!(), "libshadow_shim.so"));
    } else {
        const SHIM_LIB: &[u8] = b"";
    }
}

cfg_if::cfg_if! {
    if #[cfg(all(not(feature = "cargo-clippy"), not(miri), not(doctest), not(loom), not(doc)))] {
        const INJECTOR_LIB: &[u8] =
            std::include_bytes!("../../../build/src/lib/preload-injector/libshadow_injector.so");
    } else {
        const INJECTOR_LIB: &[u8] = b"";
    }
}

cfg_if::cfg_if! {
    if #[cfg(all(not(feature = "cargo-clippy"), not(miri), not(doctest), not(loom), not(doc)))] {
        const LIBC_LIB: &[u8] =
            std::include_bytes!("../../../build/src/lib/preload-libc/libshadow_libc.so");
    } else {
        const LIBC_LIB: &[u8] = b"";
    }
}

cfg_if::cfg_if! {
    if #[cfg(all(not(feature = "cargo-clippy"), not(miri), not(doctest), not(loom), not(doc)))] {
        const OPENSSL_RNG_LIB: &[u8] =
            std::include_bytes!("../../../build/src/lib/preload-openssl/libshadow_openssl_rng.so");
    } else {
        const OPENSSL_RNG_LIB: &[u8] = b"";
    }
}

cfg_if::cfg_if! {
    if #[cfg(all(not(feature = "cargo-clippy"), not(miri), not(doctest), not(loom), not(doc)))] {
        const OPENSSL_CRYPTO_LIB: &[u8] =
            std::include_bytes!("../../../build/src/lib/preload-openssl/libshadow_openssl_crypto.so");
    } else {
        const OPENSSL_CRYPTO_LIB: &[u8] = b"";
    }
}

pub fn init(config: &ConfigOptions) -> anyhow::Result<PreloadFiles> {
    let with_preload_libs = config
        .experimental
        .with_preload_libs
        .as_ref()
        .unwrap()
        .as_ref()
        .to_option();

    if let Some(path) = with_preload_libs {
        if !path.is_dir() {
            anyhow::bail!("The directory {} does not exist", path.display());
        }
        Ok(init_from_existing(path))
    } else {
        init_from_embedded(config)
    }
}

/// Initialize the preload files from embedded files.
fn init_from_embedded(config: &ConfigOptions) -> anyhow::Result<PreloadFiles> {
    // Rather than letting the managed processes inherit the fd and using the "/proc/self/fd/<fd>"
    // preload path, we could instead use CLOEXEC and set "/proc/<our-pid>/fd/<fd>" for the preload
    // path. But I don't see an advantage since the number of open files should be the same either
    // way.

    // TODO: replace these with C string literals in Rust 1.76
    fn as_cstr(null_terminated_bytes: &[u8]) -> &CStr {
        CStr::from_bytes_with_nul(null_terminated_bytes).unwrap()
    }

    let mut files = Vec::new();

    // we always preload the injector lib to ensure that the shim is loaded into the managed
    {
        let name = as_cstr(b"libshadow_injector.so\0");
        files.push(PreloadFile::new_memfd(name, INJECTOR_LIB)?);
    }

    // preload libc lib if option is enabled
    if config.experimental.use_preload_libc.unwrap() {
        let name = as_cstr(b"libshadow_libc.so\0");
        files.push(PreloadFile::new_memfd(name, LIBC_LIB)?);
    } else {
        log::info!("Preloading the libc library is disabled");
    }

    // preload openssl rng lib if option is enabled
    if config.experimental.use_preload_openssl_rng.unwrap() {
        let name = as_cstr(b"libshadow_openssl_rng.so\0");
        files.push(PreloadFile::new_memfd(name, OPENSSL_RNG_LIB)?);
    } else {
        log::info!("Preloading the openssl rng library is disabled");
    }

    // preload openssl crypto lib if option is enabled
    if config.experimental.use_preload_openssl_crypto.unwrap() {
        let name = as_cstr(b"libshadow_openssl_crypto.so\0");
        files.push(PreloadFile::new_memfd(name, OPENSSL_CRYPTO_LIB)?);
    } else {
        log::info!("Preloading the openssl crypto library is disabled");
    }

    let shim_file = new_memfd(as_cstr(b"libshadow_shim.so\0"), SHIM_LIB)?;
    let shim_symlink_dir = tempfile::tempdir().context("Could not create temporary directory")?;

    std::os::unix::fs::symlink(
        file_path(&shim_file, /* use_pid= */ false),
        shim_symlink_dir.path().join("libshadow_shim.so"),
    )
    .context("Could not create shim symlink")?;

    Ok(PreloadFiles {
        files,
        shim: ShimFile::MemFd(MemFdShimFile {
            symlink_dir: shim_symlink_dir,
            _file: shim_file,
        }),
    })
}

/// Initialize the preload files from existing files.
fn init_from_existing(path: impl AsRef<Path>) -> PreloadFiles {
    let path = path.as_ref();

    let preload_names = [
        "libshadow_injector.so",
        "libshadow_libc.so",
        "libshadow_openssl_rng.so",
        "libshadow_openssl_crypto.so",
    ];

    let files = preload_names
        .iter()
        .map(|name| PreloadFile::new_existing(path.join(name)))
        .collect();

    PreloadFiles {
        files,
        shim: ShimFile::Existing(path.to_path_buf()),
    }
}

pub fn dump_libraries(path: impl AsRef<Path>) -> anyhow::Result<()> {
    let path = path.as_ref();

    if !path.is_dir() {
        anyhow::bail!("The directory {} does not exist", path.display());
    }

    std::fs::write(path.join("libshadow_shim.so"), SHIM_LIB)
        .context("Could not write libshadow_shim.so")?;
    std::fs::write(path.join("libshadow_injector.so"), INJECTOR_LIB)
        .context("Could not write libshadow_injector.so")?;
    std::fs::write(path.join("libshadow_libc.so"), LIBC_LIB)
        .context("Could not write libshadow_libc.so")?;
    std::fs::write(path.join("libshadow_openssl_rng.so"), OPENSSL_RNG_LIB)
        .context("Could not write libshadow_openssl_rng.so")?;
    std::fs::write(path.join("libshadow_openssl_crypto.so"), OPENSSL_CRYPTO_LIB)
        .context("Could not write libshadow_openssl_crypto.so")?;

    Ok(())
}

pub struct PreloadFiles {
    pub files: Vec<PreloadFile>,
    shim: ShimFile,
}

pub struct PreloadFile {
    pub path: PathBuf,
    _file: Option<File>,
}

enum ShimFile {
    MemFd(MemFdShimFile),
    Existing(PathBuf),
}

struct MemFdShimFile {
    symlink_dir: tempfile::TempDir,
    _file: File,
}

impl PreloadFiles {
    pub fn shim_dir(&self) -> &Path {
        match self.shim {
            ShimFile::MemFd(ref x) => x.symlink_dir.path(),
            ShimFile::Existing(ref x) => x,
        }
    }
}

impl PreloadFile {
    fn new_memfd(name: &CStr, contents: &[u8]) -> anyhow::Result<Self> {
        let file = new_memfd(name, contents)?;
        let path = file_path(&file, /* use_pid= */ false);

        Ok(Self {
            path,
            _file: Some(file),
        })
    }

    fn new_existing(path: PathBuf) -> Self {
        Self { path, _file: None }
    }
}

fn new_memfd(name: &CStr, contents: &[u8]) -> anyhow::Result<File> {
    // don't use CLOEXEC here so that the managed process can refer to it using '/proc/self/'
    let fd = nix::sys::memfd::memfd_create(name, MemFdCreateFlag::empty())
        .context("Could not create memfd")?;

    let mut file = File::from(fd);
    file.write_all(contents)
        .context("Could not write to memfd")?;

    Ok(file)
}

fn file_path(file: &File, use_pid: bool) -> PathBuf {
    let mut path = PathBuf::new();

    let pid = if use_pid {
        std::process::id().to_string()
    } else {
        "self".to_string()
    };

    path.push("/proc");
    path.push(pid);
    path.push("fd");
    path.push(&file.as_raw_fd().to_string());

    path
}
