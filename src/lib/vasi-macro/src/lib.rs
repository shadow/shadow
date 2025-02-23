// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

use proc_macro2::TokenStream;
use quote::quote;
use syn::{Attribute, GenericParam, Generics, Type, parse_quote};

/// Implement `vasi::VirtualAddressSpaceIndependent` for the annotated type.
/// Requires all fields to implement `vasi::VirtualAddressSpaceIndependent`.
///
/// An empty struct fails becase Rust doesn't consider fieldless structs to be
/// FFI-safe:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[derive(VirtualAddressSpaceIndependent)]
/// #[repr(C)]
/// struct Foo {}
/// ```
///
/// FFI-safe structs containing only `VirtualAddressSpaceIndependent`
/// fields qualify:
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {
///   x: i32,
/// }
/// ```
///
/// `#[repr(transparent)]` is OK too.
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(transparent)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {
///   x: i32,
/// }
/// ```
///
/// A struct containing a *reference* doesn't qualify:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo<'a> {
///   x: &'a i32,
/// }
/// ```
///
/// A struct containing a [Box] doesn't qualify:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {
///   x: Box<i32>,
/// }
/// ```
///
/// A struct containing a *pointer* doesn't qualify:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {
///   x: *const i32,
/// }
/// ```
///
/// A field can be allow-listed with the attribute `unsafe_assume_virtual_address_space_independent`:
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {
///   // SAFETY: we ensure the pointer isn't dereferenced
///   // outside of its original virtual address space.
///   #[unsafe_assume_virtual_address_space_independent]
///   x: *const i32,
/// }
/// ```
///
/// A union containing only `VirtualAddressSpaceIndependent` fields qualifies:
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// union Foo {
///   x: i32,
///   y: i32,
/// }
/// ```
///
/// A union containing a non-vasi member doesn't qualify:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {
///   x: i32,
///   y: *const i32,
/// }
/// ```
///
/// An enum containing only `VirtualAddressSpaceIndependent` variants qualifies:
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// enum Foo {
///   Bar(i32),
///   Baz(i32),
/// }
/// ```
///
/// An enum containing a non-vasi variant doesn't qualify:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// enum Foo {
///   Bar(i32),
///   Baz(*const i32),
/// }
/// ```
///
/// A generic type *conditionally* implements VirtualAddressSpaceIndependent,
/// if its type parameters do (as the derive macros in the std crate behave).
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct MyWrapper<T> {
///   val: T,
/// }
///
/// static_assertions::assert_impl_all!(MyWrapper<i32>: vasi::VirtualAddressSpaceIndependent);
/// static_assertions::assert_not_impl_all!(MyWrapper<* const i32>: vasi::VirtualAddressSpaceIndependent);
/// ```
///
/// Generic type with existing bounds are also supported.
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct MyWrapper<T: Copy> {
///   val: T,
/// }
/// static_assertions::assert_impl_all!(MyWrapper<i32>: vasi::VirtualAddressSpaceIndependent);
/// static_assertions::assert_not_impl_all!(MyWrapper<* const i32>: vasi::VirtualAddressSpaceIndependent);
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct MyWrapper2<T> where T: Copy {
///   val: T,
/// }
/// static_assertions::assert_impl_all!(MyWrapper2<i32>: vasi::VirtualAddressSpaceIndependent);
/// static_assertions::assert_not_impl_all!(MyWrapper2<* const i32>: vasi::VirtualAddressSpaceIndependent);
/// ```
///
/// As with e.g. Copy and Clone, a field that is dependent on a type parameter
/// but still isn't VirtualAddressSpaceIndependent will cause the macro not to
/// compile:
/// ```compile_fail
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[repr(C)]
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct MyWrapper<T> {
///   val: *const T,
/// }
/// ```
#[proc_macro_derive(
    VirtualAddressSpaceIndependent,
    attributes(unsafe_assume_virtual_address_space_independent)
)]
pub fn derive_virtual_address_space_independent(
    tokens: proc_macro::TokenStream,
) -> proc_macro::TokenStream {
    // Construct a representation of Rust code as a syntax tree
    // that we can manipulate
    let ast = syn::parse(tokens).unwrap();
    // Build the trait implementation
    impl_derive_virtual_address_space_independent(ast)
}

// Add a bound `T: VirtualAddressSpaceIndependent` to every type parameter T.
fn add_trait_bounds(mut generics: Generics) -> Generics {
    for param in &mut generics.params {
        if let GenericParam::Type(ref mut type_param) = *param {
            type_param
                .bounds
                .push(parse_quote!(vasi::VirtualAddressSpaceIndependent));
        }
    }
    generics
}

fn assume_vasi(attrs: &[Attribute]) -> bool {
    attrs.iter().any(|attr| {
        attr.path()
            .is_ident("unsafe_assume_virtual_address_space_independent")
    })
}

fn impl_derive_virtual_address_space_independent(ast: syn::DeriveInput) -> proc_macro::TokenStream {
    let name = &ast.ident;
    // This will contain calls to a function `check` that accepts VirtualAddressSpaceIndependent types,
    // which is how we validate that the fields are VirtualAddressSpaceIndependent.
    // e.g. for an input struct definition
    // ```
    // struct MyStruct {
    //   x: u32,
    //   y: i32,
    // }
    // ```
    //
    // We'll end up generating code like:
    // ```
    // impl VirtualAddressSpaceIndependent for MyStruct {
    //     const IGNORE: () = {
    //         fn check<T: VirtualAddressSpaceIndependent>() {}
    //         check::<u32>(); // check type of MyStruct::x
    //         check::<i32>(); // check type of MyStruct::y
    //     };
    // }
    // ```
    let types: Vec<&Type> = match &ast.data {
        syn::Data::Struct(s) => s
            .fields
            .iter()
            .filter(|field| !assume_vasi(&field.attrs))
            .map(|field| &field.ty)
            .collect(),
        syn::Data::Enum(e) => e
            .variants
            .iter()
            .flat_map(|variant| {
                variant
                    .fields
                    .iter()
                    .filter(|field| !assume_vasi(&field.attrs))
                    .map(|field| &field.ty)
            })
            .collect(),
        syn::Data::Union(u) => u
            .fields
            .named
            .iter()
            .filter(|field| !assume_vasi(&field.attrs))
            .map(|field| &field.ty)
            .collect(),
    };

    // These will fail to compile if any of the types aren't VirtualAddressSpaceIndependent.
    let calls_to_check: TokenStream = types
        .into_iter()
        .map(|ty| quote! {check::<#ty>();})
        .collect();

    // Add a bound `T: VirtualAddressSpaceIndependent` to every type parameter T.
    // This allows generic types to be conditionally VirtualAddressSpaceIndependent,
    // iff their type parameters are.
    let generics = add_trait_bounds(ast.generics);
    let (impl_generics, ty_generics, where_clause) = generics.split_for_impl();

    quote! {
        unsafe impl #impl_generics vasi::VirtualAddressSpaceIndependent for #name #ty_generics #where_clause {
            const IGNORE: () = {
                const fn check<T: ::vasi::VirtualAddressSpaceIndependent>() {}
                #calls_to_check
            };
        }
        #[deny(improper_ctypes_definitions)]
        const _: () = {
            // Force compilation to fail if the type isn't FFI safe.
            extern "C" fn _vasi_validate_ffi_safe #impl_generics (_: #name #ty_generics) #where_clause {}
        };
    }
    .into()
}
