#[cfg(test)]
mod tests {
    use grokj2k_sys::*;
    use std::ffi::{CStr, CString};
    use std::ptr;

    fn init() {
        unsafe {
            grk_initialize(ptr::null(), 0, ptr::null_mut());
        }
    }

    fn set_file_path(buf: &mut [::std::os::raw::c_char; 4096], path: &str) {
        let c_str = CString::new(path).unwrap();
        let bytes = c_str.as_bytes_with_nul();
        for (i, &b) in bytes.iter().enumerate() {
            buf[i] = b as ::std::os::raw::c_char;
        }
    }

    // --- Version tests ---

    #[test]
    fn test_version_returns_non_null() {
        unsafe {
            let ver = grk_version();
            assert!(!ver.is_null());
        }
    }

    #[test]
    fn test_version_format() {
        unsafe {
            let ver = CStr::from_ptr(grk_version());
            let s = ver.to_str().unwrap();
            assert!(!s.is_empty());
            let parts: Vec<&str> = s.split('.').collect();
            assert!(parts.len() >= 2, "Version should have major.minor: {}", s);
        }
    }

    // --- Initialization tests ---

    #[test]
    fn test_initialize() {
        init();
    }

    // --- Constants tests ---

    #[test]
    fn test_color_space_values() {
        assert_eq!(_GRK_COLOR_SPACE_GRK_CLRSPC_SRGB, 2);
        assert_eq!(_GRK_COLOR_SPACE_GRK_CLRSPC_GRAY, 3);
    }

    #[test]
    fn test_file_format_values() {
        assert_eq!(GRK_FMT_J2K, 1);
        assert_eq!(GRK_FMT_JP2, 2);
    }

    // --- Compress params tests ---

    #[test]
    fn test_default_params() {
        init();
        unsafe {
            let mut params: grk_cparameters = std::mem::zeroed();
            grk_compress_set_default_params(&mut params);
            // Default resolution is typically 6
            assert!(params.numresolution > 0);
        }
    }

    #[test]
    fn test_modify_params() {
        init();
        unsafe {
            let mut params: grk_cparameters = std::mem::zeroed();
            grk_compress_set_default_params(&mut params);
            params.irreversible = true;
            assert!(params.irreversible);
            params.numresolution = 3;
            assert_eq!(params.numresolution, 3);
        }
    }

    // --- Image creation tests ---

    #[test]
    fn test_create_gray_image() {
        init();
        unsafe {
            let mut comp: grk_image_comp = std::mem::zeroed();
            comp.w = 64;
            comp.h = 64;
            comp.dx = 1;
            comp.dy = 1;
            comp.prec = 8;
            comp.sgnd = false;

            let img = grk_image_new(1, &mut comp, _GRK_COLOR_SPACE_GRK_CLRSPC_GRAY, true);
            assert!(!img.is_null());
            assert_eq!((*img).numcomps, 1);
            grk_object_unref(&mut (*img).obj);
        }
    }

    #[test]
    fn test_create_rgb_image() {
        init();
        unsafe {
            let mut comps: [grk_image_comp; 3] = std::mem::zeroed();
            for comp in comps.iter_mut() {
                comp.w = 32;
                comp.h = 32;
                comp.dx = 1;
                comp.dy = 1;
                comp.prec = 8;
                comp.sgnd = false;
            }

            let img = grk_image_new(
                3, comps.as_mut_ptr(), _GRK_COLOR_SPACE_GRK_CLRSPC_SRGB, true,
            );
            assert!(!img.is_null());
            assert_eq!((*img).numcomps, 3);

            // Check component access
            let comp0 = &*(*img).comps;
            assert_eq!(comp0.w, 32);
            assert_eq!(comp0.h, 32);
            assert_eq!(comp0.prec, 8);

            grk_object_unref(&mut (*img).obj);
        }
    }

    // --- Decompress params tests ---

    #[test]
    fn test_create_decompress_params() {
        unsafe {
            let _params: grk_decompress_parameters = std::mem::zeroed();
        }
    }

    // --- Stream params tests ---

    #[test]
    fn test_create_stream_params() {
        unsafe {
            let _stream: grk_stream_params = std::mem::zeroed();
        }
    }

    // --- Header info tests ---

    #[test]
    fn test_create_header_info() {
        unsafe {
            let _header: grk_header_info = std::mem::zeroed();
        }
    }

    // --- Round-trip test ---

    #[test]
    fn test_compress_decompress_roundtrip() {
        init();
        let tmp = std::env::temp_dir().join("grok_rust_test.j2k");
        let tmp_path = tmp.to_str().unwrap();

        unsafe {
            // --- Compress ---
            let mut params: grk_cparameters = std::mem::zeroed();
            grk_compress_set_default_params(&mut params);
            params.cod_format = GRK_FMT_J2K;

            let mut comp: grk_image_comp = std::mem::zeroed();
            comp.w = 32;
            comp.h = 32;
            comp.dx = 1;
            comp.dy = 1;
            comp.prec = 8;
            comp.sgnd = false;

            let img = grk_image_new(
                1, &mut comp, _GRK_COLOR_SPACE_GRK_CLRSPC_GRAY, true,
            );
            assert!(!img.is_null());

            let mut stream: grk_stream_params = std::mem::zeroed();
            set_file_path(&mut stream.file, tmp_path);

            let codec = grk_compress_init(&mut stream, &mut params, img);
            assert!(!codec.is_null());

            let length = grk_compress(codec, ptr::null_mut());
            assert!(length > 0, "Compressed length should be > 0");

            grk_object_unref(codec);
            grk_object_unref(&mut (*img).obj);

            assert!(tmp.exists());

            // --- Decompress ---
            let mut dstream: grk_stream_params = std::mem::zeroed();
            set_file_path(&mut dstream.file, tmp_path);

            let mut dparams: grk_decompress_parameters = std::mem::zeroed();
            let dcodec = grk_decompress_init(&mut dstream, &mut dparams);
            assert!(!dcodec.is_null());

            let mut header: grk_header_info = std::mem::zeroed();
            let read_ok = grk_decompress_read_header(dcodec, &mut header);
            assert!(read_ok);

            let dimg = grk_decompress_get_image(dcodec);
            assert!(!dimg.is_null());
            assert_eq!((*dimg).numcomps, 1);

            let dec_ok = grk_decompress(dcodec, ptr::null_mut());
            assert!(dec_ok);

            let dcomp = &*(*dimg).comps;
            assert_eq!(dcomp.w, 32);
            assert_eq!(dcomp.h, 32);

            grk_object_unref(dcodec);

            // Cleanup
            let _ = std::fs::remove_file(&tmp);
        }
    }

    // --- Layer rate test ---

    #[test]
    fn test_set_layer_rate() {
        init();
        unsafe {
            let mut params: grk_cparameters = std::mem::zeroed();
            grk_compress_set_default_params(&mut params);
            params.layer_rate[0] = 20.0;
            params.numlayers = 1;
            params.allocation_by_rate_distortion = true;
            assert!(params.allocation_by_rate_distortion);
            assert_eq!(params.layer_rate[0], 20.0);
        }
    }

    // --- Error handling tests ---

    #[test]
    fn test_decompress_nonexistent_file() {
        init();
        unsafe {
            let mut stream: grk_stream_params = std::mem::zeroed();
            set_file_path(&mut stream.file, "/tmp/nonexistent_grk_file_12345.j2k");

            let mut params: grk_decompress_parameters = std::mem::zeroed();
            let codec = grk_decompress_init(&mut stream, &mut params);
            // Init may succeed but read_header should fail
            if !codec.is_null() {
                let mut header: grk_header_info = std::mem::zeroed();
                let ok = grk_decompress_read_header(codec, &mut header);
                assert!(!ok, "read_header should fail for nonexistent file");
                grk_object_unref(codec);
            }
        }
    }
}
