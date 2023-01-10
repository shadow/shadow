use proc_macro::*;
use quote::ToTokens;

// FIXME: These doctests are effectively disabled via `compile_fail`.
// For them to work, this crate would need to depend on shadow_rs. We can't do that though,
// since shadow_rs depends on this crate. This could perhaps be fixed by moving the parts
// of shadow_rs neede by this crate out to a separate crate to break the cycle.
/// This macro wraps a syscall handler by renaming the original function and making a new
/// function with the original name that calls the original function. When the syscall handler
/// function is called, it will log the syscall if syscall logging is enabled in Shadow.
///
/// For example,
///
/// ```compile_fail
/// # use syscall_logger::log_syscall;
/// # use shadow_rs::host::context::ThreadContext;
/// # use shadow_rs::host::syscall_types::{SysCallArgs, SyscallResult};
/// struct MyHandler {}
///
/// impl MyHandler {
///     #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int)]
///     pub fn close(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {}
/// }
/// ```
///
/// will become,
///
/// ```compile_fail
/// # use shadow_rs::host::context::ThreadContext;
/// # use shadow_rs::host::syscall_types::{SysCallArgs, SyscallResult};
/// struct MyHandler {}
///
/// impl MyHandler {
///     pub fn close(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
///         // ...
///         let rv = close_original(ctx, args);
///         // ...
///         rv
///     }
///     fn close_original(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
///     }
/// }
/// ```
#[proc_macro_attribute]
pub fn log_syscall(args: TokenStream, input: TokenStream) -> TokenStream {
    let mut input: syn::Item = syn::parse(input).unwrap();

    let syscall_name;
    let syscall_name_original;

    // modify the original function
    {
        let mut input_fn = match &mut input {
            syn::Item::Fn(input_fn) => input_fn,
            _ => panic!("Expected Item::Fn"),
        };

        syscall_name = input_fn.sig.ident.clone();
        syscall_name_original = quote::format_ident!("{}_original", syscall_name);

        // rename the function
        input_fn.sig.ident = syscall_name_original.clone();

        // we assume the syscall handler is public for simplicity
        assert!(std::matches!(input_fn.vis, syn::Visibility::Public(_)));

        // make the function non-public
        input_fn.vis = syn::Visibility::Inherited;
    }

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

    let arg_types: Vec<proc_macro2::TokenStream> = arg_types
        .into_iter()
        .map(|x| TokenStream::from(x).into())
        .collect();
    let rv_type: Vec<proc_macro2::TokenStream> = rv_type
        .into_iter()
        .map(|x| TokenStream::from(x).into())
        .collect();

    let syscall_name_str = syscall_name.to_string();

    let syscall_wrapper = quote::quote! {
        pub fn #syscall_name(
            &self,
            ctx: &mut crate::host::context::ThreadContext,
            args: &crate::host::syscall_types::SysCallArgs,
        ) -> crate::host::syscall_types::SyscallResult {
            let Some(strace_fmt_options) = ctx.process.strace_logging_options() else {
                // exit early if strace logging is not enabled
                return self.#syscall_name_original(ctx, args);
            };

            // make sure to include the full path to all used types
            use crate::core::worker::Worker;
            use crate::host::syscall::formatter::{SyscallArgsFmt, SyscallResultFmt, write_syscall};

            let syscall_args = {
                let memory = ctx.process.memory_borrow();
                let syscall_args = <SyscallArgsFmt::<#(#arg_types)*>>::new(args.args, strace_fmt_options, &*memory);
                // need to convert to a string so that we read the plugin's memory before we potentially
                // modify it during the syscall
                format!("{}", syscall_args)
            };

            // make the syscall
            let rv = self.#syscall_name_original(ctx, args);

            // In case the syscall borrowed memory references without flushing them,
            // we need to flush them here.
            if rv.is_ok() {
                ctx.process.free_unsafe_borrows_flush().unwrap();
            } else {
                ctx.process.free_unsafe_borrows_noflush();
            }

            // format the result (returns None if the syscall didn't complete)
            let memory = ctx.process.memory_borrow();
            let syscall_rv = SyscallResultFmt::<#(#rv_type)*>::new(&rv, args.args, strace_fmt_options, &*memory);

            if let Some(ref syscall_rv) = syscall_rv {
                ctx.process.with_strace_file(|file| {
                    write_syscall(
                        file,
                        &Worker::current_time().unwrap(),
                        ctx.thread.id(),
                        #syscall_name_str,
                        &syscall_args,
                        syscall_rv,
                    ).unwrap();
                });
            }

            rv
        }
    };

    let mut s: TokenStream = syscall_wrapper.into();
    s.extend(TokenStream::from(input.into_token_stream()));
    s
}
