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

use grokj2k_sys::*;
use std::ffi::{CString, CStr};
use std::mem;
use std::os::raw::c_char;
use std::ptr;
use grokj2k_sys::errors::GrokError;

fn str_to_slice(src: &str) -> Result<[i8; 4096], GrokError> {
    let c_string = CString::new(src).map_err(GrokError::NullError)?;
    let bytes_with_nul = c_string.as_bytes_with_nul();
    let mut buffer = [0i8; 4096];  // Initialize buffer as [i8; 4096]
    if bytes_with_nul.len() > buffer.len() {
        return Err(GrokError::BufferTooSmall);
    }
    // Convert u8 to i8 and copy
    for (i, &byte) in bytes_with_nul.iter().enumerate() {
        buffer[i] = byte as i8;
    }
    Ok(buffer)
}

/// Clean up resources safely
fn cleanup(grk_cparameters: *mut _grk_cparameters) {
    unsafe {
        grk_deinitialize();
        libc::free(grk_cparameters as *mut libc::c_void);
    }
}

fn initialize_grk() -> Result<(), GrokError> {
    let num_threads = 0;  // Pass 0 for number of threads
    let verbose = true;   // Example: set verbose to true

    unsafe {
        grk_initialize(ptr::null(), num_threads, verbose);
    }

    Ok(())
}

fn lib() -> Result<(), GrokError> {
    let infile = str_to_slice("foo.tif")?;
    let outfile = str_to_slice("bar.j2k")?;

    unsafe {
        let _ = initialize_grk();
        let version = CStr::from_ptr(grk_version());
        println!("Grok version: {}", version.to_str().unwrap());

        let grk_cparameters = libc::malloc(mem::size_of::<_grk_cparameters>()) as *mut _grk_cparameters;
        if grk_cparameters.is_null() {
            return Err(GrokError::MemoryAllocationFailed);
        }

        grk_compress_set_default_params(grk_cparameters);
        (*grk_cparameters).infile = infile;
        (*grk_cparameters).outfile = outfile;
        (*grk_cparameters).decod_format = GRK_FMT_TIF;
        (*grk_cparameters).cod_format = GRK_FMT_J2K;
        // do compression
        cleanup(grk_cparameters);
    }
    Ok(())
}

fn main() {
    match lib() {
        Ok(_) => println!("Operation completed successfully."),
        Err(e) => eprintln!("Error occurred: {:?}", e),
    }
}

