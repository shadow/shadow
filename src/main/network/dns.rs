use std::collections::HashMap;
use std::fs::File;
use std::io::Write;
use std::net::Ipv4Addr;
use std::os::fd::AsRawFd;
use std::path::PathBuf;
use std::sync::Arc;

use rustix::fs::MemfdFlags;
use shadow_shim_helper_rs::HostId;

#[derive(Debug)]
struct Database {
    // We can use `String` here because [`crate::core::configuration::HostName`] limits the
    // configured host names to a subset of ascii, which are always valid utf-8.
    name_index: HashMap<String, Arc<Record>>,
    addr_index: HashMap<Ipv4Addr, Arc<Record>>,
}

#[derive(Debug)]
struct Record {
    id: HostId,
    addr: Ipv4Addr,
    name: String,
}

#[derive(Debug)]
pub struct DnsBuilder {
    db: Database,
}

impl DnsBuilder {
    pub fn new() -> Self {
        Self {
            db: Database {
                name_index: HashMap::new(),
                addr_index: HashMap::new(),
            },
        }
    }

    pub fn register(&mut self, id: HostId, addr: Ipv4Addr, name: String) {
        if !addr.is_loopback() && !addr.is_unspecified() {
            let record = Arc::new(Record {
                id,
                addr,
                name: name.clone(),
            });
            self.db.name_index.insert(name, record.clone());
            self.db.addr_index.insert(addr, record);
        }
    }

    pub fn into_dns(self) -> anyhow::Result<Dns> {
        let pid = std::process::id();
        let name = format!("shadow_dns_hosts_file_{pid}");

        let memfd = rustix::fs::memfd_create(name, MemfdFlags::CLOEXEC)?;
        let mut file = File::from(memfd);

        // Sort the records to produce deterministic ordering in the hosts file.
        let mut records: Vec<&Arc<Record>> = self.db.addr_index.values().collect();
        // records.sort_by(|a, b| a.addr.cmp(&b.addr));
        records.sort_by_key(|x| x.addr);

        writeln!(file, "127.0.0.1 localhost")?;
        for record in records.iter() {
            // Make it easier to debug if somehow we ever got a name with whitespace.
            assert!(!record.name.as_bytes().iter().any(u8::is_ascii_whitespace));
            writeln!(file, "{} {}", record.addr, record.name)?;
        }

        Ok(Dns {
            db: self.db,
            hosts_file: file,
        })
    }
}

impl Default for DnsBuilder {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Debug)]
pub struct Dns {
    db: Database,
    // Keep this handle while Dns is valid to prevent closing the file
    // containing the hosts database in /etc/hosts format.
    hosts_file: File,
}

impl Dns {
    pub fn addr_to_host_id(&self, addr: Ipv4Addr) -> Option<HostId> {
        self.db.addr_index.get(&addr).map(|record| record.id)
    }

    #[cfg(test)]
    fn addr_to_name(&self, addr: Ipv4Addr) -> Option<&str> {
        self.db
            .addr_index
            .get(&addr)
            .map(|record| record.name.as_str())
    }

    pub fn name_to_addr(&self, name: &str) -> Option<Ipv4Addr> {
        self.db.name_index.get(name).map(|record| record.addr)
    }

    pub fn hosts_path(&self) -> PathBuf {
        PathBuf::from(format!("/proc/self/fd/{}", self.hosts_file.as_raw_fd()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn host_a() -> (HostId, Ipv4Addr, String) {
        let id = HostId::from(0);
        let addr = Ipv4Addr::new(100, 1, 2, 3);
        let name = String::from("myhost");
        (id, addr, name)
    }

    fn host_b() -> (HostId, Ipv4Addr, String) {
        let id = HostId::from(1);
        let addr = Ipv4Addr::new(200, 3, 2, 1);
        let name = String::from("theirhost");
        (id, addr, name)
    }

    #[test]
    fn lookups() {
        let (id_a, addr_a, name_a) = host_a();
        let (id_b, addr_b, name_b) = host_b();

        let mut builder = DnsBuilder::new();
        builder.register(id_a, addr_a, name_a.clone());
        builder.register(id_b, addr_b, name_b.clone());
        let dns = builder.into_dns().unwrap();

        assert_eq!(dns.addr_to_host_id(addr_a), Some(id_a));
        assert_eq!(dns.addr_to_host_id(addr_b), Some(id_b));
        assert_eq!(dns.addr_to_host_id(Ipv4Addr::new(1, 2, 3, 4)), None);

        assert_eq!(dns.addr_to_name(addr_a), Some(name_a.as_str()));
        assert_eq!(dns.addr_to_name(addr_b), Some(name_b.as_str()));
        assert_eq!(dns.addr_to_name(Ipv4Addr::new(1, 2, 3, 4)), None);

        assert_eq!(dns.name_to_addr(&name_a), Some(addr_a));
        assert_eq!(dns.name_to_addr(&name_b), Some(addr_b));
        assert_eq!(dns.name_to_addr("empty"), None);
        assert_eq!(dns.name_to_addr("localhost"), None);
    }

    #[test]
    fn hosts_file() {
        let (id_a, addr_a, name_a) = host_a();
        let (id_b, addr_b, name_b) = host_b();

        let mut builder = DnsBuilder::new();
        builder.register(id_a, addr_a, name_a.clone());
        builder.register(id_b, addr_b, name_b.clone());
        let dns = builder.into_dns().unwrap();

        let contents = std::fs::read_to_string(dns.hosts_path()).unwrap();

        let expected = "127.0.0.1 localhost\n100.1.2.3 myhost\n200.3.2.1 theirhost\n";
        assert_eq!(contents.as_str(), expected);
        let unexpected = "127.0.0.1 localhost\n200.3.2.1 theirhost\n100.1.2.3 myhost\n";
        assert_ne!(contents.as_str(), unexpected);
    }
}
