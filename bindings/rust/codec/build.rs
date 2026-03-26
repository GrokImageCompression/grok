/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
    if std::env::var("DOCS_RS").is_ok() {
        return;
    }

    // Find the codec library via pkg-config
    let lib = pkg_config::Config::new()
        .atleast_version("12.0.0")
        .probe("libgrokj2kcodec")
        .expect("Failed to find 'libgrokj2kcodec' with pkg-config. Ensure that the library is installed and pkg-config is configured.");

    let include_paths: Vec<String> = lib.include_paths.iter()
        .map(|path| path.to_str().unwrap_or_else(|| panic!("Failed to convert path to string")).to_string())
        .collect();

    // Find grok_codec.h
    let header_path = include_paths.iter()
        .find_map(|path| {
            let full_path = format!("{}/grok_codec.h", path);
            if PathBuf::from(&full_path).exists() {
                Some(full_path)
            } else {
                None
            }
        })
        .expect("Failed to find 'grok_codec.h' in any pkg-config include paths");

    let mut builder = bindgen::Builder::default()
        .header(header_path)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()));

    for include_path in &include_paths {
        builder = builder.clang_arg(format!("-I{}", include_path));
    }

    let bindings = builder
        .generate()
        .expect("Unable to generate bindings from grok_codec.h");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings to file");

    for flag in lib.libs.iter() {
        println!("cargo:rustc-link-lib={}", flag);
    }

    for flag in lib.link_paths.iter() {
        println!("cargo:rustc-link-search=native={}", flag.display());
    }
}
