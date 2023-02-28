use proc_macro::TokenStream;
use quote::quote;

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
/// TODO: Extend to support trait bounds. See e.g.
/// <https://github.com/dtolnay/syn/blob/master/examples/heapsize/heapsize_derive/src/lib.rs#L16>
#[proc_macro_derive(
    VirtualAddressSpaceIndependent,
    attributes(unsafe_assume_virtual_address_space_independent)
)]
pub fn derive_virtual_address_space_independent(tokens: TokenStream) -> TokenStream {
    // Construct a representation of Rust code as a syntax tree
    // that we can manipulate
    let ast = syn::parse(tokens).unwrap();
    // Build the trait implementation
    impl_derive_virtual_address_space_independent(ast)
}

fn impl_derive_virtual_address_space_independent(ast: syn::DeriveInput) -> TokenStream {
    let name = &ast.ident;
    let mut output = quote! {};
    match &ast.data {
        syn::Data::Struct(s) => {
            for field in &s.fields {
                if field.attrs.iter().any(|a| {
                    a.path
                        .is_ident("unsafe_assume_virtual_address_space_independent")
                }) {
                    // Explicitly opted out of assertion.
                    continue;
                }
                let t = &field.ty;
                output = quote! {
                    #output
                    // This is static_assertions::assert_impl_all, re-exported
                    // for use in code generated here.
                    vasi::assert_impl_all!(#t: vasi::VirtualAddressSpaceIndependent);
                };
            }
        }
        syn::Data::Enum(_) => unimplemented!("Enums aren't yet implemented"),
        syn::Data::Union(_) => unimplemented!("Unions aren't yet implemented"),
    };
    let gen = quote! {
        #output
        /// SAFETY: All fields are vasi::VirtualAddressSpaceIndependent.
        unsafe impl vasi::VirtualAddressSpaceIndependent for #name {}
    };
    gen.into()
}
