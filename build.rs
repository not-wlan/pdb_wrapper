use cmake::Config;
use std::{env, path::PathBuf};

fn main() {
    let dst = Config::new("libllvm-pdb-wrapper").build();
    println!("cargo:rustc-link-search=native={}", dst.display());
    println!("cargo:rustc-link-lib=static=llvm-pdb-wrapper");

    println!("cargo:rustc-link-lib=llvm-pdb-wrapper");
    println!("cargo:rustc-link-lib=LLVM");
    println!("cargo:rustc-link-lib=stdc++");

    println!("cargo:rerun-if-changed=libllvm-pdb-wrapper/wrapper.hpp");
    println!("cargo:rerun-if-changed=libllvm-pdb-wrapper/wrapper.cpp");
    println!("cargo:rerun-if-changed={}", dst.display());
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
