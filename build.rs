use cmake::Config;
use std::{env, path::PathBuf, process::Command};
use which::which;

#[cfg(all(feature = "llvm_10", feature = "llvm_13"))]
compile_error!("You may only enable one LLVM version");

#[cfg(feature = "llvm_10")]
const LLVM_VERSION: u32 = 10;

#[cfg(feature = "llvm_13")]
const LLVM_VERSION: u32 = 13;

#[cfg(feature = "llvm_10")]
fn version_specific_init() {
    println!("cargo:rustc-link-lib=LLVM-10");
}

#[cfg(feature = "llvm_13")]
fn version_specific_init() {
    // TODO: Does this work as expected on Windows?
    let binary = which("llvm-config")
        .expect("llvm-config was not found, please make sure that it's contained in your PATH!");

    // Get required libraries from `llvm-config`
    let result = Command::new(binary)
        .arg("--libnames")
        .arg("--ignore-libllvm")
        .arg("DebugInfoPDB")
        .output()
        .expect("Failed to run `llvm-config`");

    let result = String::from_utf8(result.stdout).expect("Failed to parse `llvm-config` output!");
    if cfg!(linux) {
        result
            .trim()
            .split(" ")
            .map(|lib| lib.trim())
            .filter(|lib| !lib.is_empty())
            .for_each(|lib| {
                println!("cargo:rustc-link-lib={}", lib);
            });
    } else if cfg!(windows) {
        result
            .trim().replace(".lib","")
            .split(" ")
            .map(|lib| lib.trim())
            .filter(|lib| !lib.is_empty())
            .for_each(|lib| {
                println!("cargo:rustc-link-lib={}", lib);
            });
    }

}

fn main() {
    let dst = Config::new("libllvm-pdb-wrapper").build();
    println!("cargo:rustc-link-search=native={}", dst.display());
    println!("cargo:rerun-if-changed={}", dst.display());

    version_specific_init();

    if cfg!(unix) {
        // TODO: Test if this is required on Windows too?
        println!("cargo:rustc-link-lib=ncurses");
    }
    if cfg!(linux) {
        println!("cargo:rustc-link-lib=z");
        println!("cargo:rustc-link-lib=stdc++");
    } else if cfg!(windows) {
        println!("cargo:rustc-link-lib=zlib");
    }
    println!("cargo:rustc-link-lib=static=llvm-pdb-wrapper");
    println!("cargo:rustc-link-lib=llvm-pdb-wrapper");

    println!("cargo:rerun-if-changed=libllvm-pdb-wrapper/wrapper.hpp");
    println!("cargo:rerun-if-changed=libllvm-pdb-wrapper/wrapper.cpp");

    let bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("libllvm-pdb-wrapper/wrapper.hpp")
        .clang_arg(format!("-DLLVM_VERSION_MAJOR={}", LLVM_VERSION))
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
