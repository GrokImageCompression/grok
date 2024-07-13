/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

extern crate bindgen;
extern crate pkg_config;

use std::env;
use std::path::PathBuf;

fn main() {
    // Skip build configuration when generating documentation with docs.rs
    if std::env::var("DOCS_RS").is_ok() {
        return;
    }

    // Using pkg-config to find the installed Grok JPEG 2000 library
    let lib = pkg_config::Config::new()
        .atleast_version("12.0.0")
        .probe("libgrokj2k")
        .expect("Failed to find 'libgrokj2k' with pkg-config. Ensure that the library is installed and pkg-config is configured.");

    // Collecting include paths provided by pkg-config to find header files
    let include_paths: Vec<String> = lib.include_paths.iter()
        .map(|path| path.to_str().unwrap_or_else(|| panic!("Failed to convert path to string")).to_string())
        .collect();

    // Finding the 'grok.h' header file among the include paths
    let header_path = include_paths.iter()
        .find_map(|path| {
            let full_path = format!("{}/grok.h", path);
            if PathBuf::from(&full_path).exists() {
                Some(full_path)
            } else {
                None
            }
        })
        .expect("Failed to find 'grok.h' in any pkg-config include paths");

    // Setup bindgen builder to generate Rust bindings from the C header
    let mut builder = bindgen::Builder::default()
        .header(header_path)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks));

    // Including the found include paths for clang argument in bindgen
    for include_path in &include_paths {
        builder = builder.clang_arg(format!("-I{}", include_path));
    }

    // Generating the bindings
    let bindings = builder
        .generate()
        .expect("Unable to generate bindings from the provided header");

    // Writing the bindings to the $OUT_DIR/bindings.rs file
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings to file");

    // Informing Cargo on how to link with the native Grok library dynamically
    for flag in lib.libs.iter() {
        println!("cargo:rustc-link-lib={}", flag);
    }

    for flag in lib.link_paths.iter() {
        println!("cargo:rustc-link-search=native={}", flag.display());
    }
}

