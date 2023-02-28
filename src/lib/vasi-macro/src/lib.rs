use proc_macro2::TokenStream;
use quote::quote;
use syn::Field;

/// Implement `vasi::VirtualAddressSpaceIndependent` for the annotated type.
/// Requires all fields to implement `vasi::VirtualAddressSpaceIndependent`.
///
/// An empty struct trivially qualifies:
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
/// #[derive(VirtualAddressSpaceIndependent)]
/// struct Foo {}
/// ```
///
/// A struct containing only `VirtualAddressSpaceIndependent` fields qualifies:
/// ```
/// use vasi::VirtualAddressSpaceIndependent;
///
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
/// #[derive(VirtualAddressSpaceIndependent)]
/// enum Foo {
///   Bar(i32),
///   Baz(*const i32),
/// }
/// ```
///
/// TODO: Extend to support trait bounds. See e.g.
/// <https://github.com/dtolnay/syn/blob/master/examples/heapsize/heapsize_derive/src/lib.rs#L16>
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

fn assertions_for_field(field: &Field) -> TokenStream {
    if field.attrs.iter().any(|a| {
        a.path
            .is_ident("unsafe_assume_virtual_address_space_independent")
    }) {
        // Explicitly opted out of assertion.
        TokenStream::new()
    } else {
        let t = &field.ty;
        quote! {
            // This is static_assertions::assert_impl_all, re-exported
            // for use in code generated here.
            vasi::assert_impl_all!(#t: vasi::VirtualAddressSpaceIndependent);
        }
    }
}

fn impl_derive_virtual_address_space_independent(ast: syn::DeriveInput) -> proc_macro::TokenStream {
    let name = &ast.ident;
    let mut assertions = Vec::new();
    match &ast.data {
        syn::Data::Struct(s) => {
            for field in &s.fields {
                assertions.push(assertions_for_field(field));
            }
        }
        syn::Data::Enum(e) => {
            for variant in &e.variants {
                for field in &variant.fields {
                    assertions.push(assertions_for_field(field));
                }
            }
        }
        syn::Data::Union(u) => {
            for field in &u.fields.named {
                assertions.push(assertions_for_field(field));
            }
        }
    };
    let assertions: TokenStream = assertions.into_iter().collect();
    let gen = quote! {
        #assertions
        /// SAFETY: All fields are vasi::VirtualAddressSpaceIndependent.
        unsafe impl vasi::VirtualAddressSpaceIndependent for #name {}
    };
    gen.into()
}
