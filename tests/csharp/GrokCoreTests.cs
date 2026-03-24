using Xunit;
using System;
using System.IO;

namespace GrokCoreTests;

public class VersionTests
{
    [Fact]
    public void VersionReturnsNonEmptyString()
    {
        string version = grok_core_csharp.grk_version();
        Assert.False(string.IsNullOrEmpty(version));
    }

    [Fact]
    public void VersionMatchesExpectedFormat()
    {
        string version = grok_core_csharp.grk_version();
        // Version format: "major.minor.patch"
        var parts = version.Split('.');
        Assert.True(parts.Length >= 2, $"Version should have at least major.minor: {version}");
        Assert.True(int.TryParse(parts[0], out _), $"Major version should be numeric: {parts[0]}");
    }
}

public class InitializationTests
{
    [Fact]
    public void InitializeDoesNotThrow()
    {
        grok_core_csharp.grk_initialize(null, 0, null);
    }
}

public class ConstantsTests
{
    [Fact]
    public void ColorSpaceEnumsExist()
    {
        Assert.Equal(2, (int)GRK_COLOR_SPACE.GRK_CLRSPC_SRGB);
        Assert.Equal(3, (int)GRK_COLOR_SPACE.GRK_CLRSPC_GRAY);
    }

    [Fact]
    public void FileFormatConstantsExist()
    {
        Assert.Equal(1, (int)GRK_SUPPORTED_FILE_FMT.GRK_FMT_J2K);
        Assert.Equal(2, (int)GRK_SUPPORTED_FILE_FMT.GRK_FMT_JP2);
    }
}

public class CompressParamsTests
{
    [Fact]
    public void DefaultParamsCreatable()
    {
        grok_core_csharp.grk_initialize(null, 0, null);
        var p = new grk_cparameters();
        grok_core_csharp.grk_compress_set_default_params(p);
        Assert.NotNull(p);
    }

    [Fact]
    public void CanSetIrreversible()
    {
        var p = new grk_cparameters();
        grok_core_csharp.grk_compress_set_default_params(p);
        p.irreversible = false;
        Assert.False(p.irreversible);
        p.irreversible = true;
        Assert.True(p.irreversible);
    }

    [Fact]
    public void CanSetResolution()
    {
        var p = new grk_cparameters();
        grok_core_csharp.grk_compress_set_default_params(p);
        p.numresolution = 3;
        Assert.Equal(3u, p.numresolution);
    }
}

public class DecompressParamsTests
{
    [Fact]
    public void CreateDecompressParams()
    {
        var p = new grk_decompress_parameters();
        Assert.NotNull(p);
    }
}

public class ImageCreationTests
{
    [Fact]
    public void CreateUniformGrayImage()
    {
        grok_core_csharp.grk_initialize(null, 0, null);
        var img = grok_core_csharp.grk_image_new_uniform(
            1, 64, 64, 1, 1, 8, false, GRK_COLOR_SPACE.GRK_CLRSPC_GRAY);
        Assert.NotNull(img);
        Assert.Equal((ushort)1, img.numcomps);
        grok_core_csharp.grk_object_unref(img.obj);
    }

    [Fact]
    public void CreateUniformRGBImage()
    {
        grok_core_csharp.grk_initialize(null, 0, null);
        var img = grok_core_csharp.grk_image_new_uniform(
            3, 128, 128, 1, 1, 8, false, GRK_COLOR_SPACE.GRK_CLRSPC_SRGB);
        Assert.NotNull(img);
        Assert.Equal((ushort)3, img.numcomps);
        grok_core_csharp.grk_object_unref(img.obj);
    }

    [Fact]
    public void GetComponentViaExtend()
    {
        grok_core_csharp.grk_initialize(null, 0, null);
        var img = grok_core_csharp.grk_image_new_uniform(
            3, 32, 32, 1, 1, 8, false, GRK_COLOR_SPACE.GRK_CLRSPC_SRGB);
        Assert.NotNull(img);

        var comp = img.get_comp(0);
        Assert.NotNull(comp);
        Assert.Equal(32u, comp.w);
        Assert.Equal(32u, comp.h);
        Assert.Equal((byte)8, comp.prec);

        grok_core_csharp.grk_object_unref(img.obj);
    }
}

public class StreamParamsTests
{
    [Fact]
    public void CreateStreamParams()
    {
        var sp = new grk_stream_params();
        Assert.NotNull(sp);
    }

    [Fact]
    public void SetFilePath()
    {
        var sp = new grk_stream_params();
        sp.file = "/tmp/test.j2k";
        Assert.Equal("/tmp/test.j2k", sp.file);
    }
}

public class RoundTripTests
{
    [Fact]
    public void CompressAndDecompressGray()
    {
        grok_core_csharp.grk_initialize(null, 0, null);

        string tmpFile = Path.Combine(Path.GetTempPath(), $"grok_csharp_test_{Guid.NewGuid()}.j2k");
        try
        {
            // Compress
            var cparams = new grk_cparameters();
            grok_core_csharp.grk_compress_set_default_params(cparams);
            cparams.cod_format = GRK_SUPPORTED_FILE_FMT.GRK_FMT_J2K;

            var img = grok_core_csharp.grk_image_new_uniform(
                1, 32, 32, 1, 1, 8, false, GRK_COLOR_SPACE.GRK_CLRSPC_GRAY);
            Assert.NotNull(img);

            var stream = new grk_stream_params();
            stream.file = tmpFile;

            var codec = grok_core_csharp.grk_compress_init(stream, cparams, img);
            Assert.NotNull(codec);

            ulong length = grok_core_csharp.grk_compress(codec, null);
            Assert.True(length > 0, "Compressed length should be > 0");

            grok_core_csharp.grk_object_unref(codec);
            grok_core_csharp.grk_object_unref(img.obj);

            Assert.True(File.Exists(tmpFile));

            // Decompress
            var dstream = new grk_stream_params();
            dstream.file = tmpFile;

            var dparams = new grk_decompress_parameters();
            var dcodec = grok_core_csharp.grk_decompress_init(dstream, dparams);
            Assert.NotNull(dcodec);

            var header = new grk_header_info();
            bool readOk = grok_core_csharp.grk_decompress_read_header(dcodec, header);
            Assert.True(readOk);

            var dimg = grok_core_csharp.grk_decompress_get_image(dcodec);
            Assert.NotNull(dimg);
            Assert.Equal((ushort)1, dimg.numcomps);

            bool decOk = grok_core_csharp.grk_decompress(dcodec, null);
            Assert.True(decOk);

            var comp = dimg.get_comp(0);
            Assert.NotNull(comp);
            Assert.Equal(32u, comp.w);
            Assert.Equal(32u, comp.h);

            grok_core_csharp.grk_object_unref(dcodec);
        }
        finally
        {
            if (File.Exists(tmpFile))
                File.Delete(tmpFile);
        }
    }
}

public class HeaderInfoTests
{
    [Fact]
    public void CreateHeaderInfo()
    {
        var h = new grk_header_info();
        Assert.NotNull(h);
    }
}

public class LayerRateHelperTests
{
    [Fact]
    public void SetLayerRate()
    {
        var p = new grk_cparameters();
        grok_core_csharp.grk_compress_set_default_params(p);
        grok_core_csharp.grk_cparameters_set_layer_rate(p, 0, 20.0);
        p.numlayers = 1;
        p.allocation_by_rate_distortion = true;
        // If we get here without exception, the helper works
        Assert.True(p.allocation_by_rate_distortion);
    }
}
