use std::collections::HashMap;
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use rand::seq::SliceRandom;

use crate::host::descriptor::socket::unix::{UnixSocket, UnixSocketType};
use crate::host::descriptor::FileState;
use crate::host::descriptor::{StateEventSource, StateListenHandle, StateListenerFilter};

struct NamespaceEntry {
    /// The bound socket.
    socket: Weak<AtomicRefCell<UnixSocket>>,
    /// The event listener handle, which removes the listener when dropped.
    _handle: StateListenHandle,
}

impl NamespaceEntry {
    pub fn new(socket: Weak<AtomicRefCell<UnixSocket>>, handle: StateListenHandle) -> Self {
        Self {
            socket,
            _handle: handle,
        }
    }
}

pub struct AbstractUnixNamespace {
    address_map: HashMap<UnixSocketType, HashMap<Vec<u8>, NamespaceEntry>>,
}

impl AbstractUnixNamespace {
    pub fn new() -> Self {
        let mut rv = Self {
            // initializes an empty hash map for each unix socket type
            address_map: HashMap::new(),
        };

        // the namespace code will assume that there is an entry for each socket type
        rv.address_map
            .insert(UnixSocketType::Stream, HashMap::new());
        rv.address_map.insert(UnixSocketType::Dgram, HashMap::new());
        rv.address_map
            .insert(UnixSocketType::SeqPacket, HashMap::new());

        rv
    }

    pub fn lookup(
        &self,
        sock_type: UnixSocketType,
        name: &[u8],
    ) -> Option<Arc<AtomicRefCell<UnixSocket>>> {
        // the unwrap() will panic if the socket was dropped without being closed, but this should
        // only be possible at the end of the simulation and there wouldn't be any reason to call
        // lookup() at that time, so a panic here would most likely indicate an issue somewhere else
        // in shadow
        self.address_map
            .get(&sock_type)
            .unwrap()
            .get(name)
            .map(|x| x.socket.upgrade().unwrap())
    }

    pub fn bind(
        ns_arc: &Arc<AtomicRefCell<Self>>,
        sock_type: UnixSocketType,
        mut name: Vec<u8>,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        socket_event_source: &mut StateEventSource,
    ) -> Result<(), BindError> {
        // make sure we aren't wasting memory since we don't mutate the name
        name.shrink_to_fit();

        let mut ns = ns_arc.borrow_mut();
        let name_copy = name.clone();

        // look up the name in the address map
        let entry = match ns.address_map.get_mut(&sock_type).unwrap().entry(name) {
            std::collections::hash_map::Entry::Occupied(_) => return Err(BindError::NameInUse),
            std::collections::hash_map::Entry::Vacant(x) => x,
        };

        // when the socket closes, remove this entry from the namespace
        let handle =
            Self::on_socket_close(Arc::downgrade(ns_arc), socket_event_source, move |ns| {
                assert!(ns.unbind(sock_type, &name_copy).is_ok());
            });

        entry.insert(NamespaceEntry::new(Arc::downgrade(socket), handle));

        Ok(())
    }

    pub fn autobind(
        ns_arc: &Arc<AtomicRefCell<Self>>,
        sock_type: UnixSocketType,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        socket_event_source: &mut StateEventSource,
        mut rng: impl rand::Rng,
    ) -> Result<Vec<u8>, BindError> {
        let mut ns = ns_arc.borrow_mut();

        // the unused name that we will bind the socket to
        let mut name = None;

        // try 10 random names
        for _ in 0..10 {
            let random_name: [u8; NAME_LEN] = random_name(&mut rng);

            if !ns
                .address_map
                .get(&sock_type)
                .unwrap()
                .contains_key(&random_name[..])
            {
                name = Some(random_name.to_vec());
                break;
            }
        }

        // if unsuccessful, try a linear search through all valid names
        if name.is_none() {
            for x in 0..CHARSET.len().pow(NAME_LEN as u32) {
                let temp_name: [u8; NAME_LEN] = incremental_name(x);

                if !ns
                    .address_map
                    .get(&sock_type)
                    .unwrap()
                    .contains_key(&temp_name[..])
                {
                    name = Some(temp_name.to_vec());
                    break;
                }
            }
        }

        let name = match name {
            Some(x) => x,
            // every valid name has been taken
            None => return Err(BindError::NoNamesAvailable),
        };

        let name_copy = name.clone();

        // when the socket closes, remove this entry from the namespace
        let handle =
            Self::on_socket_close(Arc::downgrade(ns_arc), socket_event_source, move |ns| {
                assert!(ns.unbind(sock_type, &name_copy).is_ok());
            });

        if let std::collections::hash_map::Entry::Vacant(entry) = ns
            .address_map
            .get_mut(&sock_type)
            .unwrap()
            .entry(name.clone())
        {
            entry.insert(NamespaceEntry::new(Arc::downgrade(socket), handle));
        } else {
            unreachable!();
        }

        Ok(name)
    }

    pub fn unbind(&mut self, sock_type: UnixSocketType, name: &Vec<u8>) -> Result<(), BindError> {
        // remove the namespace entry which includes the handle, so the event listener will
        // automatically be removed from the socket
        if self
            .address_map
            .get_mut(&sock_type)
            .unwrap()
            .remove(name)
            .is_none()
        {
            // didn't exist in the address map
            return Err(BindError::NameNotFound);
        }

        Ok(())
    }

    /// Adds a listener to the socket which runs the callback `f` when the socket is closed.
    fn on_socket_close(
        ns: Weak<AtomicRefCell<Self>>,
        event_source: &mut StateEventSource,
        f: impl Fn(&mut Self) + Send + Sync + 'static,
    ) -> StateListenHandle {
        event_source.add_listener(
            FileState::CLOSED,
            StateListenerFilter::OffToOn,
            move |state, _changed, _signals, _cb_queue| {
                assert!(state.contains(FileState::CLOSED));
                if let Some(ns) = ns.upgrade() {
                    f(&mut ns.borrow_mut());
                }
            },
        )
    }
}

impl Default for AbstractUnixNamespace {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Debug, Clone, Copy)]
pub enum BindError {
    /// The name is already in use.
    NameInUse,
    /// Names in the ephemeral name range are all in use.
    NoNamesAvailable,
    /// The name was not found in the address map.
    NameNotFound,
}

impl std::error::Error for BindError {}

impl std::fmt::Display for BindError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NameInUse => write!(f, "Name is already in use"),
            Self::NoNamesAvailable => {
                write!(f, "Names in the ephemeral name range are all in use")
            }
            Self::NameNotFound => write!(f, "Name was not found in the address map"),
        }
    }
}

/// The characters that are valid in auto-generated names; see subsection "Autobind feature" in
/// unix(7).
const CHARSET: &[u8] = b"abcdef0123456789";
const NAME_LEN: usize = 5;

/// Choose a random name of length `L`.
fn random_name<const L: usize>(mut rng: impl rand::Rng) -> [u8; L] {
    let mut name = [0u8; L];

    // set each character of the name
    for c in &mut name {
        *c = *CHARSET.choose(&mut rng).unwrap();
    }

    name
}

/// Get a name in the set of all valid names. This is essentially the n'th element in the cartesian
/// power of set `CHARSET`. This would be better implemented as a generator when generators become
/// stable.
fn incremental_name<const L: usize>(mut index: usize) -> [u8; L] {
    const CHARSET_LEN: usize = CHARSET.len();

    // there are a limited number of valid names
    assert!(index < CHARSET_LEN.pow(L as u32));

    let mut name = [0u8; L];

    // set each character of the name
    for x in 0..L {
        // take the base-10 index and convert it to base-CHARSET_LEN digits
        let charset_index = index % CHARSET_LEN;
        index /= CHARSET_LEN;

        // set the name in reverse order
        name[L - x - 1] = CHARSET[charset_index];
    }

    name
}

#[cfg(test)]
mod tests {
    use rand_core::SeedableRng;
    use rand_xoshiro::Xoshiro256PlusPlus;

    use super::*;

    #[test]
    fn test_random_name() {
        let mut rng = Xoshiro256PlusPlus::seed_from_u64(0);

        let name_1: [u8; 5] = random_name(&mut rng);
        let name_2: [u8; 5] = random_name(&mut rng);

        assert!(name_1.iter().all(|x| CHARSET.contains(x)));
        assert!(name_2.iter().all(|x| CHARSET.contains(x)));
        assert_ne!(name_1, name_2);
    }

    #[test]
    fn test_incremental_name() {
        assert_eq!(incremental_name::<5>(0), [b'a', b'a', b'a', b'a', b'a']);
        assert_eq!(incremental_name::<5>(1), [b'a', b'a', b'a', b'a', b'b']);
        assert_eq!(
            incremental_name::<5>(CHARSET.len()),
            [b'a', b'a', b'a', b'b', b'a']
        );
        assert_eq!(
            incremental_name::<5>(CHARSET.len() + 1),
            [b'a', b'a', b'a', b'b', b'b']
        );
        assert_eq!(
            incremental_name::<5>(CHARSET.len().pow(5) - 1),
            [b'9', b'9', b'9', b'9', b'9']
        );
    }

    #[test]
    #[should_panic]
    fn test_incremental_name_panic() {
        incremental_name::<5>(CHARSET.len().pow(5));
    }
}
