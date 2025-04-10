use vasi::VirtualAddressSpaceIndependent;

#[derive(
    Copy,
    Clone,
    Debug,
    Default,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Hash,
    VirtualAddressSpaceIndependent,
)]
#[repr(C)]
pub enum FfiOption<T> {
    #[default]
    None,
    Some(T),
}

impl<T> FfiOption<T> {
    pub fn unwrap(self) -> T {
        match self {
            Self::Some(x) => x,
            Self::None => panic!("called `FfiOption::unwrap()` on a `None` value"),
        }
    }

    pub fn unwrap_or(self, default: T) -> T {
        match self {
            Self::Some(x) => x,
            Self::None => default,
        }
    }

    pub fn take(&mut self) -> Self {
        let mut other = Self::None;
        core::mem::swap(self, &mut other);
        other
    }

    pub fn replace(&mut self, value: T) -> Self {
        let mut other = Self::Some(value);
        core::mem::swap(self, &mut other);
        other
    }

    pub fn as_ref(&self) -> FfiOption<&T> {
        match self {
            Self::Some(x) => FfiOption::Some(x),
            Self::None => FfiOption::None,
        }
    }

    pub fn as_mut(&mut self) -> FfiOption<&mut T> {
        match self {
            Self::Some(x) => FfiOption::Some(x),
            Self::None => FfiOption::None,
        }
    }
}

impl<T> From<Option<T>> for FfiOption<T> {
    fn from(x: Option<T>) -> Self {
        match x {
            Some(x) => Self::Some(x),
            None => Self::None,
        }
    }
}
