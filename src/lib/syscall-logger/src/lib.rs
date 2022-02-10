use proc_macro::*;
use quote::ToTokens;

/// This macro wraps a syscall handler function by renaming the original function and making a new
/// function with the original name that calls the original function. When the syscall handler
/// function is called, it will log the syscall if syscall logging is enabled in Shadow.
///
/// For example,
///
/// ```
/// #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int)]
/// pub fn close(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {}
/// ```
///
/// will become,
///
/// ```
/// pub fn close(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
///     ...
///     let rv = close_original(ctx, args);
///     ...
///     rv
/// }
/// fn close_original(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
/// }
/// ```
#[proc_macro_attribute]
pub fn log_syscall(args: TokenStream, input: TokenStream) -> TokenStream {
    let mut item: syn::Item = syn::parse(input.clone()).unwrap();
    let mut fn_item = match &mut item {
        syn::Item::Fn(fn_item) => fn_item,
        _ => panic!("expected fn"),
    };

    let syscall_name = fn_item.sig.ident.to_string();

    // rename the function
    fn_item.sig.ident = syn::Ident::new(
        &(syscall_name.clone() + "_original"),
        fn_item.sig.ident.span(),
    );

    // we assume the syscall handler is public for simplicity
    assert!(std::matches!(fn_item.vis, syn::Visibility::Public(_)));

    // make the function non-public
    fn_item.vis = syn::Visibility::Inherited;

    let mut rv_type = vec![];
    let mut arg_types = vec![];

    let mut found_comma = false;
    for tt in args.into_iter() {
        if let TokenTree::Punct(ref p) = tt {
            if !found_comma && p.as_char() == ',' && p.spacing() == Spacing::Alone {
                found_comma = true;
                continue;
            }
        }
        if !found_comma {
            rv_type.push(tt);
        } else {
            arg_types.push(tt);
        }
    }

    let x = format!(
        r#"
        pub fn {syscall_name}(
            ctx: &mut crate::host::context::ThreadContext,
            args: &crate::host::syscall_types::SysCallArgs,
        ) -> crate::host::syscall_types::SyscallResult {{
            // exit early if strace logging is not enabled
            if !ctx.process.strace_logging_enabled() {{
                return {syscall_name}_original(ctx, args);
            }}

            // make sure to include the full path to all used types
            use crate::core::worker::Worker;
            use crate::host::syscall::format::{{SyscallArgsFmt, SyscallResultFmt, write_syscall}};

            let syscall_args = SyscallArgsFmt::<{syscall_args}>::new(args, ctx.process.memory());
            // need to convert to a string so that we read the plugin's memory before we potentially
            // modify it during the syscall
            let syscall_args = format!("{{}}", syscall_args);

            // make the syscall
            let rv = {syscall_name}_original(ctx, args);

            // format the result (returns None if the syscall didn't complete)
            let syscall_rv = SyscallResultFmt::<{syscall_rv}>::new(&rv, ctx.process.memory());

            if let Some(ref syscall_rv) = syscall_rv {{
                ctx.process.with_strace_file(|file| {{
                    write_syscall(
                        file,
                        &Worker::current_time().unwrap(),
                        ctx.thread.id(),
                        "{syscall_name}",
                        &syscall_args,
                        syscall_rv,
                    ).unwrap();
                }});
            }}

            rv
        }}
        "#,
        syscall_name = &syscall_name,
        syscall_rv = TokenStream::from_iter(rv_type),
        syscall_args = TokenStream::from_iter(arg_types),
    );

    let mut s: TokenStream = x.parse().unwrap();
    s.extend(TokenStream::from(item.into_token_stream()));
    s
}
