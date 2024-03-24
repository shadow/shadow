// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

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
/// # use shadow_rs::host::syscall::handler::SyscallContext;
/// # use shadow_rs::host::syscall_types::{SyscallArgs, SyscallError};
/// struct MyHandler {}
///
/// impl MyHandler {
///     #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int)]
///     pub fn close(ctx: &mut SyscallContext, fd: libc::c_int) -> Result<libc::c_int, SyscallError> {}
/// }
/// ```
///
/// will become,
///
/// ```compile_fail
/// # use syscall_logger::log_syscall;
/// # use shadow_rs::host::syscall::handler::SyscallContext;
/// # use shadow_rs::host::syscall_types::{SyscallArgs, SyscallError};
/// struct MyHandler {}
///
/// impl MyHandler {
///     pub fn close(ctx: &mut SyscallContext, fd: libc::c_int) -> Result<libc::c_int, SyscallError> {
///         // ...
///         let rv = close_original(ctx, fd);
///         // ...
///         rv
///     }
///     fn close_original(ctx: &mut SyscallContext, fd: libc::c_int) -> Result<libc::c_int, SyscallError> {
///     }
/// }
/// ```
#[proc_macro_attribute]
pub fn log_syscall(args: TokenStream, input: TokenStream) -> TokenStream {
    let mut input: syn::Item = syn::parse(input).unwrap();

    // name of the syscall
    let syscall_name;
    // name of the syscall with "_original" appended
    let syscall_name_original;
    // the syscall argument names (ex: ["ctx", "fd", "val"])
    let syscall_args: Vec<_>;
    // the syscall arguments as a token stream (ex: "ctx: SyscallContext, fd: u32, val: i32")
    let mut syscall_args_and_types;
    // the syscal return type (including the `->` token)
    let syscall_ret_type;
    // the name of the first argument, which should be of type `SyscallContext`
    let context_arg_name;

    {
        let input_fn: &mut _ = match &mut input {
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

        // get the function arguments
        syscall_args_and_types = input_fn.sig.inputs.clone();
        for input in &mut syscall_args_and_types {
            let syn::FnArg::Typed(arg) = input else {
                continue;
            };

            let syn::Pat::Ident(ident_pat) = &mut *arg.pat else {
                continue;
            };

            // remove the `mut` keyword on arguments for our wrapper function
            ident_pat.mutability = None;
        }

        // get the names of the function arguments
        syscall_args = input_fn
            .sig
            .inputs
            .iter()
            .map(|arg| {
                let syn::FnArg::Typed(arg) = arg else {
                    panic!("Expected a typed arg. Does the function take `self`?");
                };

                // rust functions can be complicated (for example struct destructured args), but syscall
                // arguments will be simple
                let syn::Pat::Ident(ident_pat) = &*arg.pat else {
                    panic!(
                        "Function arguments must be identities (ex: `name: Type`), not {:?}",
                        arg.pat
                    );
                };

                ident_pat.ident.clone()
            })
            .collect();

        syscall_ret_type = input_fn.sig.output.clone();

        context_arg_name = syscall_args[0].clone();
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
            #syscall_args_and_types
        ) #syscall_ret_type {
            let Some(strace_fmt_options) = #context_arg_name.objs.process.strace_logging_options() else {
                // exit early if strace logging is not enabled
                return Self::#syscall_name_original(#(#syscall_args),*);
            };

            // make sure to include the full path to all used types
            use crate::core::worker::Worker;
            use crate::host::syscall::formatter::{SyscallArgsFmt, SyscallResultFmt, write_syscall};

            let syscall_args = {
                let memory = #context_arg_name.objs.process.memory_borrow();
                let syscall_args = <SyscallArgsFmt::<#(#arg_types)*>>::new(#context_arg_name.args.args, strace_fmt_options, &*memory);
                // need to convert to a string so that we read the plugin's memory before we potentially
                // modify it during the syscall
                format!("{}", syscall_args)
            };

            // make the syscall
            let rv = Self::#syscall_name_original(#(#syscall_args),*);

            // In case the syscall borrowed memory references without flushing them,
            // we need to flush them here.
            if rv.is_ok() {
                #context_arg_name.objs.process.free_unsafe_borrows_flush().unwrap();
            } else {
                #context_arg_name.objs.process.free_unsafe_borrows_noflush();
            }

            let memory = #context_arg_name.objs.process.memory_borrow();

            // Ugly hack to convert the `Result<T, SyscallError>` to a `SyscallResult` (so the `T`
            // to a `SyscallReg`) without cloning the `SyscallError`. Since we need to convert back
            // to a `Result<T, SyscallError>` later, we keep a copy of the original `Result::Ok(T)`
            // value. This assumes that `T: Into<SyscallReg> + Clone`.
            let rv_original_ok = rv.as_ref().ok().cloned();
            let rv = rv.map(|x| x.into());

            // format the result (returns None if the syscall didn't complete)
            let syscall_rv = SyscallResultFmt::<#(#rv_type)*>::new(&rv, #context_arg_name.args.args, strace_fmt_options, &*memory);

            #context_arg_name.objs.process.with_strace_file(|file| {
                write_syscall(
                    file,
                    &Worker::current_time().unwrap(),
                    #context_arg_name.objs.thread.id(),
                    #syscall_name_str,
                    &syscall_args,
                    syscall_rv,
                ).unwrap();
            });

            // convert the `SyscallResult` back to the original `Result<T, SyscallError>`
            let rv = if let Some(rv_original_ok) = rv_original_ok {
                Ok(rv_original_ok)
            } else {
                Err(rv.err().unwrap())
            };

            rv
        }
    };

    let mut s: TokenStream = syscall_wrapper.into();
    s.extend(TokenStream::from(input.into_token_stream()));
    s
}
