package org.grok.core;

import java.io.File;
import java.math.BigInteger;
import java.nio.file.Files;

/**
 * Standalone test runner for Grok Java SWIG bindings.
 * Each test method returns true on success, false on failure.
 */
public class GrokCoreTests {

    static {
        System.loadLibrary("grok_core_java");
    }

    private static int passed = 0;
    private static int failed = 0;

    private static void check(String name, boolean condition) {
        if (condition) {
            System.out.println("  PASSED: " + name);
            passed++;
        } else {
            System.out.println("  FAILED: " + name);
            failed++;
        }
    }

    // --- Version tests ---
    static void testVersionReturnsString() {
        String version = grok_core_java.grk_version();
        check("version returns non-null", version != null);
        check("version not empty", !version.isEmpty());
    }

    static void testVersionFormat() {
        String version = grok_core_java.grk_version();
        String[] parts = version.split("\\.");
        check("version has major.minor", parts.length >= 2);
    }

    // --- Initialization tests ---
    static void testInitialize() {
        // Should not throw
        grok_core_java.grk_initialize(null, 0, null);
        check("initialize succeeds", true);
    }

    // --- Constants tests ---
    static void testColorSpaces() {
        check("GRK_CLRSPC_SRGB == 2",
              GRK_COLOR_SPACE.GRK_CLRSPC_SRGB.swigValue() == 2);
        check("GRK_CLRSPC_GRAY == 3",
              GRK_COLOR_SPACE.GRK_CLRSPC_GRAY.swigValue() == 3);
    }

    // --- Compress params tests ---
    static void testDefaultParams() {
        grk_cparameters p = new grk_cparameters();
        grok_core_java.grk_compress_set_default_params(p);
        check("params created", p != null);
    }

    static void testSetIrreversible() {
        grk_cparameters p = new grk_cparameters();
        grok_core_java.grk_compress_set_default_params(p);
        p.setIrreversible(false);
        check("irreversible initially false", !p.getIrreversible());
        p.setIrreversible(true);
        check("irreversible set to true", p.getIrreversible());
    }

    static void testSetResolution() {
        grk_cparameters p = new grk_cparameters();
        grok_core_java.grk_compress_set_default_params(p);
        p.setNumresolution((short) 3);
        check("resolution set to 3", p.getNumresolution() == 3);
    }

    // --- Decompress params tests ---
    static void testCreateDecompressParams() {
        grk_decompress_parameters p = new grk_decompress_parameters();
        check("decompress params created", p != null);
    }

    // --- Image creation tests ---
    static void testCreateGrayImage() {
        grok_core_java.grk_initialize(null, 0, null);
        grk_image img = grok_core_java.grk_image_new_uniform(
            (short) 1, 64, 64, (short) 1, (short) 1, (short) 8, false,
            GRK_COLOR_SPACE.GRK_CLRSPC_GRAY);
        check("gray image created", img != null);
        check("gray image has 1 component", img.getNumcomps() == 1);
        grok_core_java.grk_object_unref(img.getObj());
    }

    static void testCreateRGBImage() {
        grok_core_java.grk_initialize(null, 0, null);
        grk_image img = grok_core_java.grk_image_new_uniform(
            (short) 3, 128, 128, (short) 1, (short) 1, (short) 8, false,
            GRK_COLOR_SPACE.GRK_CLRSPC_SRGB);
        check("rgb image created", img != null);
        check("rgb image has 3 components", img.getNumcomps() == 3);
        grok_core_java.grk_object_unref(img.getObj());
    }

    static void testGetComponent() {
        grok_core_java.grk_initialize(null, 0, null);
        grk_image img = grok_core_java.grk_image_new_uniform(
            (short) 3, 32, 32, (short) 1, (short) 1, (short) 8, false,
            GRK_COLOR_SPACE.GRK_CLRSPC_SRGB);
        check("image created for get_comp", img != null);
        grk_image_comp comp = img.get_comp(0);
        check("component 0 not null", comp != null);
        check("component width == 32", comp.getW() == 32);
        check("component height == 32", comp.getH() == 32);
        check("component prec == 8", comp.getPrec() == 8);
        grok_core_java.grk_object_unref(img.getObj());
    }

    // --- Stream params tests ---
    static void testStreamParams() {
        grk_stream_params sp = new grk_stream_params();
        check("stream params created", sp != null);
    }

    static void testStreamSetFile() {
        grk_stream_params sp = new grk_stream_params();
        sp.setFile("/tmp/test.j2k");
        check("file path set", "/tmp/test.j2k".equals(sp.getFile()));
    }

    // --- Round-trip test ---
    static void testCompressDecompress() throws Exception {
        grok_core_java.grk_initialize(null, 0, null);

        File tmpFile = File.createTempFile("grok_java_test_", ".j2k");
        tmpFile.deleteOnExit();
        String path = tmpFile.getAbsolutePath();

        // Compress
        grk_cparameters cparams = new grk_cparameters();
        grok_core_java.grk_compress_set_default_params(cparams);
        cparams.setCod_format(GRK_SUPPORTED_FILE_FMT.GRK_FMT_J2K);

        grk_image img = grok_core_java.grk_image_new_uniform(
            (short) 1, 32, 32, (short) 1, (short) 1, (short) 8, false,
            GRK_COLOR_SPACE.GRK_CLRSPC_GRAY);
        check("compress - image created", img != null);

        grk_stream_params stream = new grk_stream_params();
        stream.setFile(path);

        grk_object codec = grok_core_java.grk_compress_init(stream, cparams, img);
        check("compress - codec init", codec != null);

        BigInteger length = grok_core_java.grk_compress(codec, null);
        check("compress - length > 0", length.compareTo(BigInteger.ZERO) > 0);

        grok_core_java.grk_object_unref(codec);
        grok_core_java.grk_object_unref(img.getObj());

        check("compress - file exists", tmpFile.exists());
        check("compress - file not empty", tmpFile.length() > 0);

        // Decompress
        grk_stream_params dstream = new grk_stream_params();
        dstream.setFile(path);

        grk_decompress_parameters dparams = new grk_decompress_parameters();
        grk_object dcodec = grok_core_java.grk_decompress_init(dstream, dparams);
        check("decompress - codec init", dcodec != null);

        grk_header_info header = new grk_header_info();
        boolean readOk = grok_core_java.grk_decompress_read_header(dcodec, header);
        check("decompress - read header", readOk);

        grk_image dimg = grok_core_java.grk_decompress_get_image(dcodec);
        check("decompress - got image", dimg != null);
        check("decompress - 1 component", dimg.getNumcomps() == 1);

        boolean decOk = grok_core_java.grk_decompress(dcodec, null);
        check("decompress - success", decOk);

        grk_image_comp comp = dimg.get_comp(0);
        check("decompress - comp width 32", comp.getW() == 32);
        check("decompress - comp height 32", comp.getH() == 32);

        grok_core_java.grk_object_unref(dcodec);
        tmpFile.delete();
    }

    // --- Layer rate helper test ---
    static void testSetLayerRate() {
        grk_cparameters p = new grk_cparameters();
        grok_core_java.grk_compress_set_default_params(p);
        grok_core_java.grk_cparameters_set_layer_rate(p, (short) 0, 20.0);
        p.setNumlayers((short) 1);
        p.setAllocation_by_rate_distortion(true);
        check("layer rate helper works", p.getAllocation_by_rate_distortion());
    }

    // --- Header info test ---
    static void testCreateHeaderInfo() {
        grk_header_info h = new grk_header_info();
        check("header info created", h != null);
    }

    public static void main(String[] args) throws Exception {
        System.out.println("Grok Java Bindings Tests");
        System.out.println("========================");

        testVersionReturnsString();
        testVersionFormat();
        testInitialize();
        testColorSpaces();
        testDefaultParams();
        testSetIrreversible();
        testSetResolution();
        testCreateDecompressParams();
        testCreateGrayImage();
        testCreateRGBImage();
        testGetComponent();
        testStreamParams();
        testStreamSetFile();
        testCompressDecompress();
        testSetLayerRate();
        testCreateHeaderInfo();

        System.out.println();
        System.out.println("Results: " + passed + " passed, " + failed + " failed");

        if (failed > 0) {
            System.exit(1);
        }
    }
}
