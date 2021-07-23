use cmake::Config;
use std::{env, path::PathBuf};

fn main() {
    println!("Remember to set the LIBCLANG_PATH and/or LLVM_DIR if needed");
    if std::env::var_os("CARGO_FEATURE_LLVM_13").is_some() {
        let dst = Config::new("libllvm-pdb-wrapper")
            .define("LLVM_NEWER", "ON")
            .build();
        println!("cargo:rustc-link-search=native={}", dst.display());
        println!("cargo:rerun-if-changed={}", dst.display());
    } else {
        let dst = Config::new("libllvm-pdb-wrapper").build();
        println!("cargo:rustc-link-search=native={}", dst.display());
        println!("cargo:rerun-if-changed={}", dst.display());
    }

    println!("cargo:rustc-link-lib=static=llvm-pdb-wrapper");
    println!("cargo:rustc-link-lib=llvm-pdb-wrapper");
    if std::env::var_os("CARGO_FEATURE_LLVM_13").is_some() {
        println!("cargo:rustc-link-lib=LLVMDebugInfoCodeView");
        println!("cargo:rustc-link-lib=LLVMDebugInfoPDB");
        println!("cargo:rustc-link-lib=LLVMDebugInfoMSF");
        println!("cargo:rustc-link-lib=LLVMMC");
        println!("cargo:rustc-link-lib=LLVMCore");
        println!("cargo:rustc-link-lib=LLVMBinaryFormat");
        println!("cargo:rustc-link-lib=LLVMRemarks");
        println!("cargo:rustc-link-lib=LLVMBitstreamReader");
        println!("cargo:rustc-link-lib=LLVMMCParser");
        println!("cargo:rustc-link-lib=LLVMObject");
        println!("cargo:rustc-link-lib=LLVMTextAPI");
        println!("cargo:rustc-link-lib=LLVMBitReader");
        println!("cargo:rustc-link-lib=LLVMSupport");
        println!("cargo:rustc-link-lib=zlib");
    } else {
        println!("cargo:rustc-link-lib=LLVM-10");
        println!("cargo:rustc-link-lib=stdc++");
    }

    println!("cargo:rerun-if-changed=libllvm-pdb-wrapper/wrapper.hpp");
    println!("cargo:rerun-if-changed=libllvm-pdb-wrapper/wrapper.cpp");
    let bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("libllvm-pdb-wrapper/wrapper.hpp")
        .whitelist_function("PDB_File_.*")
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
