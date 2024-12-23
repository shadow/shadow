use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fmt::Display;
use std::fs::File;
use std::io::Write;
use std::net::Ipv4Addr;
use std::os::fd::AsRawFd;
use std::path::PathBuf;
use std::sync::Arc;

// The memfd syscall is not supported in our miri test environment.
#[cfg(not(miri))]
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

#[derive(Debug, PartialEq)]
pub enum RegistrationError {
    InvalidAddr,
    InvalidName,
    AddrExists,
    NameExists,
}

impl Display for RegistrationError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RegistrationError::InvalidAddr => write!(f, "address is invalid for registration"),
            RegistrationError::InvalidName => write!(f, "name is invalid for registration"),
            RegistrationError::NameExists => {
                write!(f, "a registration record already exists for name")
            }
            RegistrationError::AddrExists => {
                write!(f, "a registration record already exists for address")
            }
        }
    }
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

    pub fn register(
        &mut self,
        id: HostId,
        addr: Ipv4Addr,
        name: String,
    ) -> Result<(), RegistrationError> {
        // Make sure we don't register reserved addresses or names.
        if addr.is_loopback() || addr.is_unspecified() || addr.is_broadcast() || addr.is_multicast()
        {
            return Err(RegistrationError::InvalidAddr);
        } else if name.eq_ignore_ascii_case("localhost") {
            return Err(RegistrationError::InvalidName);
        }

        // A single HostId is allowed to register multiple name/addr mappings,
        // but only vacant addresses and names are allowed.
        match self.db.addr_index.entry(addr) {
            Entry::Occupied(_) => Err(RegistrationError::AddrExists),
            Entry::Vacant(addr_entry) => match self.db.name_index.entry(name.clone()) {
                Entry::Occupied(_) => Err(RegistrationError::NameExists),
                Entry::Vacant(name_entry) => {
                    let record = Arc::new(Record { id, addr, name });
                    addr_entry.insert(record.clone());
                    name_entry.insert(record);
                    Ok(())
                }
            },
        }
    }

    pub fn into_dns(self) -> std::io::Result<Dns> {
        // The memfd syscall is not supported in our miri test environment.
        #[cfg(miri)]
        let mut file = tempfile::tempfile()?;
        #[cfg(not(miri))]
        let mut file = {
            let name = format!("shadow_dns_hosts_file_{}", std::process::id());
            File::from(rustix::fs::memfd_create(name, MemfdFlags::CLOEXEC)?)
        };

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
    fn register() {
        let (id_a, addr_a, name_a) = host_a();
        let (id_b, addr_b, name_b) = host_b();

        let mut builder = DnsBuilder::new();

        assert!(builder.register(id_a, addr_a, name_a.clone()).is_ok());

        assert_eq!(
            builder.register(id_b, Ipv4Addr::UNSPECIFIED, name_b.clone()),
            Err(RegistrationError::InvalidAddr)
        );
        assert_eq!(
            builder.register(id_b, Ipv4Addr::BROADCAST, name_b.clone()),
            Err(RegistrationError::InvalidAddr)
        );
        assert_eq!(
            // Multicast addresses not allowed.
            builder.register(id_b, Ipv4Addr::new(224, 0, 0, 1), name_b.clone()),
            Err(RegistrationError::InvalidAddr)
        );
        assert_eq!(
            builder.register(id_b, Ipv4Addr::LOCALHOST, name_b.clone()),
            Err(RegistrationError::InvalidAddr)
        );
        assert_eq!(
            builder.register(id_b, addr_b, String::from("localhost")),
            Err(RegistrationError::InvalidName)
        );
        assert_eq!(
            builder.register(id_b, addr_a, name_b.clone()),
            Err(RegistrationError::AddrExists)
        );
        assert_eq!(
            builder.register(id_b, addr_b, name_a.clone()),
            Err(RegistrationError::NameExists)
        );

        assert!(builder.register(id_b, addr_b, name_b.clone()).is_ok());
    }

    #[test]
    fn lookups() {
        let (id_a, addr_a, name_a) = host_a();
        let (id_b, addr_b, name_b) = host_b();

        let mut builder = DnsBuilder::new();
        builder.register(id_a, addr_a, name_a.clone()).unwrap();
        builder.register(id_b, addr_b, name_b.clone()).unwrap();
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
    #[cfg_attr(miri, ignore)]
    fn hosts_file() {
        let (id_a, addr_a, name_a) = host_a();
        let (id_b, addr_b, name_b) = host_b();

        let mut builder = DnsBuilder::new();
        builder.register(id_a, addr_a, name_a.clone()).unwrap();
        builder.register(id_b, addr_b, name_b.clone()).unwrap();
        let dns = builder.into_dns().unwrap();

        let contents = std::fs::read_to_string(dns.hosts_path()).unwrap();

        let expected = "127.0.0.1 localhost\n100.1.2.3 myhost\n200.3.2.1 theirhost\n";
        assert_eq!(contents.as_str(), expected);
        let unexpected = "127.0.0.1 localhost\n200.3.2.1 theirhost\n100.1.2.3 myhost\n";
        assert_ne!(contents.as_str(), unexpected);
    }
}
