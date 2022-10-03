use shadow_build_common::ShadowBuildCommon;

#[cfg(feature = "bindings")]
fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = {
        let mut c = build_common.cbindgen_base_config();
        // Avoid re-exporting C types
        c.export.exclude.extend_from_slice(&[
            "LogLevel".into(),
            "PluginPtr".into(),
            "SysCallReg".into(),
            "SysCallArgs".into(),
            "Process".into(),
            "Host".into(),
            "Thread".into(),
            "EmulatedTime".into(),
            "SimulationTime".into(),
        ]);
        c
    };

    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    cbindgen::Builder::new()
        .with_crate(crate_dir.clone())
        .with_config(cbindgen::Config {
            include_guard: Some("main_bindings_h".into()),
            includes: vec![
                "lib/shadow-shim-helper-rs/shim_helper.h".into(),
                "main/bindings/c/bindings-opaque.h".into(),
                "main/core/worker.h".into(),
                "main/host/descriptor/descriptor_types.h".into(),
                "main/host/status_listener.h".into(),
                "main/host/syscall_handler.h".into(),
                "main/host/syscall_types.h".into(),
                "main/host/thread.h".into(),
                "main/host/tracker_types.h".into(),
                "main/routing/dns.h".into(),
                "main/routing/packet.minimal.h".into(),
            ],
            export: cbindgen::ExportConfig {
                // Generate all item types, excluding enum types.
                //
                // While the opaque items are already exported, and included here via
                // bindings-opaque.h, cbindgen doesn't see their definitions if we don't
                // re-export them again here. i.e. it appears to not actually parse the headers
                // included in the `includes` list.
                item_types: base_config
                    .export
                    .item_types
                    .iter()
                    .cloned()
                    .filter(|t| *t != cbindgen::ItemType::Enums)
                    .collect(),
                ..base_config.export.clone()
            },
            ..base_config.clone()
        })
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("./bindings/c/bindings.h");

    cbindgen::Builder::new()
        .with_crate(crate_dir.clone())
        .with_config(cbindgen::Config {
            include_guard: Some("main_opaque_bindings_h".into()),
            no_includes: true,
            export: cbindgen::ExportConfig {
                include: vec!["QDiscMode".into()],
                item_types: vec![cbindgen::ItemType::OpaqueItems, cbindgen::ItemType::Enums],
                ..base_config.export.clone()
            },
            ..base_config.clone()
        })
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("./bindings/c/bindings-opaque.h");
}

#[cfg(feature = "bindings")]
fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common.bindgen_builder()
        .header("core/logger/log_wrapper.h")
        .header("core/main.h")
        .header("core/support/config_handlers.h")
        .header("core/worker.h")
        .header("host/affinity.h")
        .header("host/descriptor/descriptor.h")
        .header("host/host.h")
        .header("host/process.h")
        .header("host/status.h")
        .header("host/status_listener.h")
        .header("host/syscall/fcntl.h")
        .header("host/syscall/ioctl.h")
        .header("host/syscall/socket.h")
        .header("host/syscall/unistd.h")
        .header("host/syscall_condition.h")
        .header("host/syscall_types.h")
        .header("host/thread.h")
        .header("routing/packet.h")
        .header("utility/rpath.h")
        .header("utility/utility.h")
        // Haven't decided how to handle glib struct types yet. Avoid using them
        // until we do.
        .blocklist_type("_?GQueue")
        // Needs GQueue
        .blocklist_type("_?LegacySocket.*")
        .blocklist_type("_?Socket.*")
        // Needs LegacySocket
        .blocklist_type("_?CompatSocket.*")
        // Uses atomics, which bindgen doesn't translate correctly.
        // https://github.com/rust-lang/rust-bindgen/issues/2151
        .blocklist_type("atomic_bool")
        .blocklist_type("_?ShimThreadSharedMem")
        .blocklist_type("_?ShimProcessSharedMem")
        .blocklist_function("thread_sharedMem")
        // glib functions
        .allowlist_function("g_quark_from_string")

        .allowlist_function("affinity_.*")
        .allowlist_function("thread_.*")
        .allowlist_function("legacyfile_close")
        .allowlist_function("legacyfile_(ref|unref)")
        .allowlist_function("legacyfile_getHandle")
        .allowlist_function("legacyfile_setHandle")
        .allowlist_function("legacyfile_shutdownHelper")
        .allowlist_function("host_.*")
        // Needs CompatSocket
        .blocklist_function("host_.*Interface")

        // used by shadow's main function
        .allowlist_function("main_.*")
        .allowlist_function("shmemcleanup_tryCleanup")
        .allowlist_function("scanRpathForLib")
        .allowlist_function("runConfigHandlers")
        .allowlist_function("rustlogger_new")
        .allowlist_function("dns_.*")

        .allowlist_function("workerpool_updateMinHostRunahead")

        .allowlist_function("process_.*")
        .allowlist_function("shadow_logger_getDefault")
        .allowlist_function("shadow_logger_shouldFilter")
        .allowlist_function("logger_get_global_start_time_micros")
        .allowlist_function("statuslistener_ref")
        .allowlist_function("statuslistener_unref")
        .allowlist_function("statuslistener_onStatusChanged")
        .allowlist_function("syscallcondition_new")
        .allowlist_function("syscallcondition_unref")
        .allowlist_function("syscallcondition_getActiveFile")
        .allowlist_function("syscallcondition_setActiveFile")
        .allowlist_function("syscallhandler_.*")
        .allowlist_function("worker_.*")
        .allowlist_function("workerc_.*")
        .allowlist_function("packet_getSourceIP")
        .allowlist_function("packet_getDestinationIP")
        .allowlist_function("packet_getSourcePort")
        .allowlist_function("packet_getDestinationPort")
        .allowlist_function("packet_getHeaderSize")
        .allowlist_function("packet_getPayloadSize")
        .allowlist_function("packet_getTotalSize")
        .allowlist_function("packet_getProtocol")
        .allowlist_function("packet_getTCPHeader")
        .allowlist_function("packet_copyPayloadShadow")
        //# Needs GQueue
        .blocklist_function("worker_finish")
        .blocklist_function("worker_bootHosts")
        .blocklist_function("worker_freeHosts")

        .blocklist_function("syscallhandler_new")
        .blocklist_function("syscallhandler_ref")
        .blocklist_function("syscallhandler_unref")
        .blocklist_function("syscallhandler_make_syscall")

        .allowlist_function("return_code_for_signal")

        .allowlist_type("Host")
        .allowlist_type("PluginPtr")
        .allowlist_type("Status")
        .allowlist_type("StatusListener")
        .allowlist_type("SysCall.*")
        .allowlist_type("LegacyFile")
        .allowlist_type("Manager")
        .allowlist_type("Trigger")
        .allowlist_type("TriggerType")
        .allowlist_type("LogInfoFlags")
        .allowlist_type("SimulationTime")
        .allowlist_type("ProtocolTCPFlags")
        .allowlist_var("CONFIG_PIPE_BUFFER_SIZE")
        .allowlist_var("SYSCALL_IO_BUFSIZE")
        .allowlist_var("SHADOW_SOMAXCONN")
        .opaque_type("LegacyFile")
        .opaque_type("Manager")
        .opaque_type("Descriptor")
        .opaque_type("OpenFile")
        .opaque_type("File")
        .opaque_type("ConfigOptions")
        .opaque_type("Logger")
        .opaque_type("DescriptorTable")
        .opaque_type("MemoryManager")
        .opaque_type("Random")
        .opaque_type("TaskRef")
        .opaque_type("GList")
        .blocklist_type("Logger")
        .blocklist_type("Timer")
        .blocklist_type("Controller")
        .blocklist_type("Counter")
        .blocklist_type("Descriptor")
        .blocklist_type("TaskRef")
        .allowlist_type("WorkerC")
        .opaque_type("WorkerC")
        .allowlist_type("WorkerPool")
        .opaque_type("WorkerPool")
        .blocklist_type("Arc_AtomicRefCell_AbstractUnixNamespace")
        .blocklist_type("HashSet_String")
        .blocklist_type("QDiscMode")
        .disable_header_comment()
        .raw_line("/* automatically generated by rust-bindgen */")
        .raw_line("use crate::core::support::configuration::ConfigOptions;")
        .raw_line("use crate::core::support::configuration::QDiscMode;")
        .raw_line("use crate::core::work::event::Event;")
        .raw_line("use crate::core::work::event_queue::ThreadSafeEventQueue;")
        .raw_line("use crate::core::work::task::TaskRef;")
        .raw_line("use crate::host::descriptor::descriptor_table::DescriptorTable;")
        .raw_line("use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;")
        .raw_line("use crate::host::descriptor::File;")
        .raw_line("use crate::host::descriptor::OpenFile;")
        .raw_line("use crate::host::memory_manager::MemoryManager;")
        .raw_line("use crate::host::syscall::format::StraceFmtMode;")
        .raw_line("use crate::host::syscall::handler::SyscallHandler;")
        .raw_line("use crate::host::timer::Timer;")
        .raw_line("use crate::utility::counter::Counter;")
        .raw_line("use crate::utility::random::Random;")

        .raw_line("use atomic_refcell::AtomicRefCell;")
        .raw_line("use logger::Logger;")
        .raw_line("use std::sync::Arc;")
        .raw_line("type Arc_AtomicRefCell_AbstractUnixNamespace = Arc<AtomicRefCell<AbstractUnixNamespace>>;")

        //# used to generate #[must_use] annotations)
        .enable_function_attribute_detection()

        //# don't generate rust bindings for c bindings of rust code)
        .blocklist_file(".*/bindings-opaque.h")
        .blocklist_file(".*/bindings.h")
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");

    bindings
        .write_to_file("cshadow.rs")
        .expect("Couldn't write bindings!");
}

fn build_remora(build_common: &ShadowBuildCommon) {
    build_common
        .cc_build()
        .cpp(true) // Switch to C++ library compilation.
        .file("host/descriptor/tcp_retransmit_tally.cc")
        .cpp_link_stdlib("stdc++")
        .compile("libremora.a");
}

fn build_shadow_c(build_common: &ShadowBuildCommon) {
    let mut build = build_common.cc_build();

    build.files(&[
        "core/logger/log_wrapper.c",
        "core/support/config_handlers.c",
        "core/main.c",
        "core/worker.c",
        "host/descriptor/descriptor.c",
        "host/status_listener.c",
        "host/descriptor/compat_socket.c",
        "host/descriptor/epoll.c",
        "host/descriptor/regular_file.c",
        "host/descriptor/socket.c",
        "host/descriptor/tcp.c",
        "host/descriptor/tcp_cong.c",
        "host/descriptor/tcp_cong_reno.c",
        "host/descriptor/timerfd.c",
        "host/descriptor/transport.c",
        "host/descriptor/udp.c",
        "host/affinity.c",
        "host/process.c",
        "host/cpu.c",
        "host/futex.c",
        "host/futex_table.c",
        "host/shimipc.c",
        "host/syscall_handler.c",
        "host/syscall_types.c",
        "host/syscall/protected.c",
        "host/syscall/clone.c",
        "host/syscall/epoll.c",
        "host/syscall/fcntl.c",
        "host/syscall/file.c",
        "host/syscall/fileat.c",
        "host/syscall/futex.c",
        "host/syscall/ioctl.c",
        "host/syscall/mman.c",
        "host/syscall/poll.c",
        "host/syscall/process.c",
        "host/syscall/select.c",
        "host/syscall/shadow.c",
        "host/syscall/signal.c",
        "host/syscall/socket.c",
        "host/syscall/time.c",
        "host/syscall/timerfd.c",
        "host/syscall/unistd.c",
        "host/syscall/uio.c",
        "host/thread.c",
        "host/host.c",
        "host/syscall_condition.c",
        "host/managed_thread.c",
        "host/network_interface.c",
        "host/network_queuing_disciplines.c",
        "host/tracker.c",
        "routing/payload.c",
        "routing/packet.c",
        "routing/address.c",
        "routing/router_queue_single.c",
        "routing/router_queue_static.c",
        "routing/router_queue_codel.c",
        "routing/router.c",
        "routing/dns.c",
        "utility/async_priority_queue.c",
        "utility/count_down_latch.c",
        "utility/priority_queue.c",
        "utility/rpath.c",
        "utility/tagged_ptr.c",
        "utility/utility.c",
    ]);
    build.compile("shadow-c");
}

fn main() {
    let deps = system_deps::Config::new().probe().unwrap();
    let build_common =
        shadow_build_common::ShadowBuildCommon::new(&std::path::Path::new("../.."), Some(deps));

    // The C bindings should be generated first since cbindgen doesn't require
    // the Rust code to be valid, whereas bindgen does require the C code to be
    // valid. If the C bindings are no longer correct, but the Rust bindings are
    // generated first, then there will be no way to correct the C bindings
    // since the Rust binding generation will always fail before the C bindings
    // can be corrected.
    #[cfg(feature = "bindings")]
    run_cbindgen(&build_common);

    #[cfg(feature = "bindings")]
    run_bindgen(&build_common);

    build_remora(&build_common);
    build_shadow_c(&build_common);
}
