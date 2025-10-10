
## Windows Benchmarks

powershell

Measure-Command {$HOME\src\grok\build\bin\Release\grk_decompress.exe -i $HOME\temp\IMG_PHR1B_P_202406071720183_SEN_7038440101-1_R1C1.JP2 -o $HOME\temp\temp.tif -t 0 -H 1}

Measure-Command {$HOME\src\grok\build\bin\Release\core_decompress.exe -i $HOME\temp\IMG_PHR1B_P_202406071720183_SEN_7038440101-1_R1C1.JP2 -t 0 -H 1}

Measure-Command {$HOME\bin\kdu_expand.exe -i $HOME\temp\IMG_PHR1B_P_202406071720183_SEN_7038440101-1_R1C1.JP2 -o $HOME\temp\temp.tif -region "{0.00000000000000000,0.00000000000000000},{0.10101109741060420,0.09905204101373573}" -num_threads 1 }

## Image Stitching

1. Create TIFFs with Fake Coordinates


```
gdal_translate -a_srs EPSG:3857 -a_ullr 0 20000 20000 0 input.JP2 input_georef1.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 20000 20000 40000 0 input.JP2 input_georef2.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 40000 20000 60000 0 input.JP2 input_georef3.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 60000 20000 80000 0 input.JP2 input_georef4.tif

gdal_translate -a_srs EPSG:3857 -a_ullr 0 40000 20000 20000 input.JP2 input_georef5.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 20000 40000 40000 20000 input.JP2 input_georef6.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 40000 40000 60000 20000 input.JP2 input_georef7.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 60000 40000 80000 20000 input.JP2 input_georef8.tif

gdal_translate -a_srs EPSG:3857 -a_ullr 0 60000 20000 40000 input.JP2 input_georef9.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 20000 60000 40000 40000 input.JP2 input_georef10.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 40000 60000 60000 40000 input.JP2 input_georef11.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 60000 60000 80000 40000 input.JP2 input_georef12.tif

gdal_translate -a_srs EPSG:3857 -a_ullr 0 80000 20000 60000 input.JP2 input_georef13.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 20000 80000 40000 60000 input.JP2 input_georef14.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 40000 80000 60000 60000 input.JP2 input_georef15.tif
gdal_translate -a_srs EPSG:3857 -a_ullr 60000 80000 80000 60000 input.JP2 input_georef16.tif
```

2. Build Virtual Raster

```
gdalbuildvrt -overwrite output.vrt \
    input_georef1.tif input_georef2.tif input_georef3.tif input_georef4.tif \
    input_georef5.tif input_georef6.tif input_georef7.tif input_georef8.tif \
    input_georef9.tif input_georef10.tif input_georef11.tif input_georef12.tif \
    input_georef13.tif input_georef14.tif input_georef15.tif input_georef16.tif

```

3. Convert to BigTIFF

```
gdal_translate -of GTiff -co BIGTIFF=YES -co TILED=NO output.vrt output.tif
```

4. Kakadu compression

```
kdu_compress -i pleiades4x4.tif -o pleiades4x4.jp2   -rate 90,80,70,60,50,40,30,20,10,9,8,7,6,5,1   Clayers=15   Stiles="{1024,1024}"   ORGgen_plt=yes Clevels=6 Corder=LRCP Cblk="{64,64}"   Cuse_sop=yes Cuse_eph=yes   Creversible=no
```

```
kdu_compress -i output.tif -o output.jp2   -rate 90,80,70,60,50,40,30,20,10,9,8,7,6,5,1   Clayers=15   Stiles="{128,128}"   ORGgen_plt=yes ORGtparts=R   Clevels=6 Corder=RPCL   Cblk="{64,64}"   Cuse_sop=yes Cuse_eph=yes   Creversible=no
```

5. Add TLM Marker

```
kdu_maketlm output.jp2 output_tlm.jp2
```
