#pragma once


/** @name Local static functions */
/*@{*/

static bool j2k_fromTileHeader(grk_j2k *p_j2k);

/**
 Transfer data from src to dest for each component, and null out src data.
 Assumption:  src and dest have the same number of components
 */
static void j2k_transfer_image_data(grk_image *src, grk_image *dest);
/**
 * Sets up the procedures to do on reading header. Developers wanting to extend the library can add their own reading procedures.
 */
static bool j2k_setup_header_reading(grk_j2k *p_j2k);

/**
 * The read header procedure.
 */
static bool j2k_read_header_procedure(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * The default encoding validation procedure without any extension.
 *
 * @param       p_j2k                   the jpeg2000 codec to validate.
 * @param       p_stream                the input stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_encoding_validation(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * The default decoding validation procedure without any extension.
 *
 * @param       p_j2k                   the jpeg2000 codec to validate.
 * @param       p_stream                                the input stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_decoding_validation(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_setup_encoding_validation(grk_j2k *p_j2k);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_setup_decoding_validation(grk_j2k *p_j2k);

/**
 * Sets up the validation ,i.e. adds the procedures to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_setup_end_compress(grk_j2k *p_j2k);

/**
 * The mct encoding validation procedure.
 *
 * @param       p_j2k                   the jpeg2000 codec to validate.
 * @param       p_stream                                the input stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_mct_validation(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Builds the tcd decoder to use to decode tile.
 */
static bool j2k_build_decoder(grk_j2k *p_j2k, BufferedStream *p_stream);
/**
 * Builds the tcd encoder to use to encode tile.
 */
static bool j2k_build_encoder(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Creates a tile-coder decoder.
 *
 * @param       p_j2k                           J2K codec.
 * @param       p_stream                        the stream to write data to.

 */
static bool j2k_create_tcd(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Executes the given procedures on the given codec.
 *
 * @param       p_j2k                   the jpeg2000 codec to execute the procedures on.
 * @param       p_procedure_list        the list of procedures to execute
 * @param       p_stream                the stream to execute the procedures on.
 *
 * @return      true                            if all the procedures were successfully executed.
 */
static bool j2k_exec(grk_j2k *p_j2k, std::vector<j2k_procedure> *p_procedure_list,
		BufferedStream *p_stream);

/**
 * Updates the rates of the tcp.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_update_rates(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Copies the decoding tile parameters onto all the tile parameters.
 * Creates also the tile decoder.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_copy_default_tcp_and_create_tcd(grk_j2k *p_j2k,
		BufferedStream *p_stream);

/**
 * Reads the lookup table containing all the marker, status and action, and returns the handler associated
 * with the marker value.
 * @param       id            Marker value to look up
 *
 * @return      the handler associated with the id.
 */
static const  grk_dec_memory_marker_handler  *  j2k_get_marker_handler(
		uint32_t id);

/**
 * Destroys a tile coding parameter structure.
 *
 * @param       p_tcp           the tile coding parameter to destroy.
 */
static void j2k_tcp_destroy(grk_tcp *p_tcp);

/**
 * Destroys the data inside a tile coding parameter structure.
 *
 * @param       p_tcp           the tile coding parameter which contain data to destroy.
 */
static void j2k_tcp_data_destroy(grk_tcp *p_tcp);

/**
 * Compare 2 a SPCod/ SPCoc elements, i.e. the coding style of a given component of a tile.
 *
 * @param       p_j2k            J2K codec.
 * @param       tile_no        Tile number
 * @param       first_comp_no  The 1st component number to compare.
 * @param       second_comp_no The 1st component number to compare.
 *
 * @return true if SPCdod are equals.
 */
static bool j2k_compare_SPCod_SPCoc(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t first_comp_no, uint32_t second_comp_no);

/**
 * Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 *
 * @param       p_j2k           J2K codec.
 * @param       tile_no       tile index
 * @param       comp_no       the component number to output.
 * @param       p_stream        the stream to write data to.

 *
 * @return FIXME DOC
 */
static bool j2k_write_SPCod_SPCoc(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t comp_no, BufferedStream *p_stream);

/**
 * Gets the size taken by writing a SPCod or SPCoc for the given tile and component.
 *
 * @param       p_j2k                   the J2K codec.
 * @param       tile_no               the tile index.
 * @param       comp_no               the component being outputted.
 *
 * @return      the number of bytes taken by the SPCod element.
 */
static uint32_t j2k_get_SPCod_SPCoc_size(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t comp_no);

/**
 * Reads a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 * @param       p_j2k           the jpeg2000 codec.
 * @param       compno          FIXME DOC
 * @param       p_header_data   the data contained in the COM box.
 * @param       header_size   the size of the data contained in the COM marker.

 */
static bool j2k_read_SPCod_SPCoc(grk_j2k *p_j2k, uint32_t compno,
		uint8_t *p_header_data, uint16_t *header_size);

/**
 * Gets the size taken by writing SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_j2k                   the J2K codec.
 * @param       tile_no               the tile index.
 * @param       comp_no               the component being outputted.
 *
 * @return      the number of bytes taken by the SPCod element.
 */
static uint32_t j2k_get_SQcd_SQcc_size(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t comp_no);

/**
 * Compares 2 SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_j2k                   J2K codec.
 * @param       tile_no               the tile to output.
 * @param       first_comp_no         the first component number to compare.
 * @param       second_comp_no        the second component number to compare.
 *
 * @return true if equals.
 */
static bool j2k_compare_SQcd_SQcc(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t first_comp_no, uint32_t second_comp_no);

/**
 * Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_j2k                   J2K codec.
 * @param       tile_no               the tile to output.
 * @param       comp_no               the component number to output.
 * @param       p_stream                the stream to write data to.

 *
 */
static bool j2k_write_SQcd_SQcc(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t comp_no, BufferedStream *p_stream);

/**
 * Updates the Tile Length Marker.
 */
static void j2k_update_tlm(grk_j2k *p_j2k, uint32_t tile_part_size);

/**
 * Reads a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param		fromQCC			true if reading QCC, otherwise false (reading QCD)
 * @param       p_j2k           J2K codec.
 * @param       compno          the component number to output.
 * @param       p_header_data   the data buffer.
 * @param       header_size   pointer to the size of the data buffer, it is changed by the function.

 *
 */
static bool j2k_read_SQcd_SQcc(bool fromQCC, grk_j2k *p_j2k, uint32_t compno,
		uint8_t *p_header_data, uint16_t *header_size);

/**
 * Copies the tile component parameters of all the component from the first tile component.
 *
 * @param               p_j2k           the J2k codec.
 */
static void j2k_copy_tile_component_parameters(grk_j2k *p_j2k);

/**
 * Reads the tiles.
 */
static bool j2k_decode_tiles(grk_j2k *p_j2k, BufferedStream *p_stream);

static bool j2k_pre_write_tile(grk_j2k *p_j2k, uint16_t tile_index);

static bool j2k_copy_decoded_tile_to_output_image(TileProcessor *p_tcd, uint8_t *p_data,
		grk_image *p_output_image, bool clearOutputOnInit);

static void j2k_get_tile_data(TileProcessor *p_tcd, uint8_t *p_data);

static bool j2k_post_write_tile(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Sets up the procedures to do on writing header.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool j2k_setup_header_writing(grk_j2k *p_j2k);

static bool j2k_write_first_tile_part(grk_j2k *p_j2k, uint64_t *p_data_written,
		uint64_t total_data_size, BufferedStream *p_stream);

static bool j2k_write_all_tile_parts(grk_j2k *p_j2k, uint64_t *p_data_written,
		uint64_t total_data_size, BufferedStream *p_stream);

/**
 * Gets the offset of the header.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_get_end_header(grk_j2k *p_j2k, BufferedStream *p_stream);

static bool j2k_allocate_tile_element_cstr_index(grk_j2k *p_j2k);

/*
 * -----------------------------------------------------------------------
 * -----------------------------------------------------------------------
 * -----------------------------------------------------------------------
 */

/**
 * Writes the SOC marker (Start Of Codestream)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                        the stream to write data to.

 */
static bool j2k_write_soc(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a SOC marker (Start of Codestream)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_stream        XXX needs data

 */
static bool j2k_read_soc(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes the SIZ marker (image and tile size)
 *
 * @param       p_j2k           J2K codec.
 * @param       p_stream        the stream to write data to.

 */
static bool j2k_write_siz(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a SIZ marker (image and tile size)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the SIZ box.
 * @param       header_size   the size of the data contained in the SIZ marker.

 */
static bool j2k_read_siz(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Reads a CAP marker
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the SIZ box.
 * @param       header_size   the size of the data contained in the SIZ marker.

 */
static bool j2k_read_cap(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the COM marker (comment)
 *
 * @param       p_j2k           J2K codec.
 * @param       p_stream        the stream to write data to.

 */
static bool j2k_write_com(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a COM marker (comments)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the COM box.
 * @param       header_size   the size of the data contained in the COM marker.

 */
static bool j2k_read_com(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Writes the COD marker (Coding style default)
 *
 * @param       p_j2k           J2K codec.
 * @param       p_stream        the stream to write data to.

 */
static bool j2k_write_cod(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a COD marker (Coding Style defaults)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the COD box.
 * @param       header_size   the size of the data contained in the COD marker.

 */
static bool j2k_read_cod(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Compares 2 COC markers (Coding style component)
 *
 * @param       p_j2k            J2K codec.
 * @param       first_comp_no  the index of the first component to compare.
 * @param       second_comp_no the index of the second component to compare.
 *
 * @return      true if equals
 */
static bool j2k_compare_coc(grk_j2k *p_j2k, uint32_t first_comp_no,
		uint32_t second_comp_no);

/**
 * Writes the COC marker (Coding style component)
 *
 * @param       p_j2k       J2K codec.
 * @param       comp_no   the index of the component to output.
 * @param       p_stream    the stream to write data to.

 */
static bool j2k_write_coc(grk_j2k *p_j2k, uint32_t comp_no,
		BufferedStream *p_stream);

/**
 * Writes the COC marker (Coding style component)
 *
 * @param       p_j2k       J2K codec.
 * @param       comp_no   the index of the component to output.
 * @param       p_stream    the stream to write data to.

 */
static bool j2k_write_coc_in_memory(grk_j2k *p_j2k, uint32_t comp_no,
		BufferedStream *p_stream);

/**
 * Gets the maximum size taken by a coc.
 *
 * @param       p_j2k   the jpeg2000 codec to use.
 */
static uint32_t j2k_get_max_coc_size(grk_j2k *p_j2k);

/**
 * Reads a COC marker (Coding Style Component)
 *
 * @param       p_j2k           the jpeg2000 codec.
 * @param       p_header_data   the data contained in the COC box.
 * @param       header_size   the size of the data contained in the COC marker.

 */
static bool j2k_read_coc(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the QCD marker (quantization default)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_qcd(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a QCD marker (Quantization defaults)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the QCD box.
 * @param       header_size   the size of the data contained in the QCD marker.

 */
static bool j2k_read_qcd(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Compare QCC markers (quantization component)
 *
 * @param       p_j2k                 J2K codec.
 * @param       first_comp_no       the index of the first component to compare.
 * @param       second_comp_no      the index of the second component to compare.
 *
 * @return true if equals.
 */
static bool j2k_compare_qcc(grk_j2k *p_j2k, uint32_t first_comp_no,
		uint32_t second_comp_no);

/**
 * Writes the QCC marker (quantization component)
 *
 * @param       p_j2k                   J2K codec.
 * @param       comp_no       the index of the component to output.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_qcc(grk_j2k *p_j2k, uint32_t comp_no,
		BufferedStream *p_stream);

/**
 * Writes the QCC marker (quantization component)
 *
 * @param       p_j2k           J2K codec.
 * @param       comp_no       the index of the component to output.
 * @param       p_stream        the stream to write data to.

 */
static bool j2k_write_qcc_in_memory(grk_j2k *p_j2k, uint32_t comp_no,
		BufferedStream *p_stream);

/**
 * Gets the maximum size taken by a qcc.
 */
static uint32_t j2k_get_max_qcc_size(grk_j2k *p_j2k);

/**
 * Reads a QCC marker (Quantization component)
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the QCC box.
 * @param       header_size   the size of the data contained in the QCC marker.

 */
static bool j2k_read_qcc(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

static uint16_t getPocSize(uint32_t l_nb_comp, uint32_t l_nb_poc);

/**
 * Writes the POC marker (Progression Order Change)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_poc(grk_j2k *p_j2k, BufferedStream *p_stream);
/**
 * Writes the POC marker (Progression Order Change)
 *
 * @param       p_j2k          J2K codec.
 * @param       p_stream       the stream to write data to.
 * @param       p_data_written number of bytes written

 */
static bool j2k_write_poc_in_memory(grk_j2k *p_j2k, BufferedStream *p_stream,
		uint64_t *p_data_written);
/**
 * Gets the maximum size taken by the writing of a POC.
 */
static uint32_t j2k_get_max_poc_size(grk_j2k *p_j2k);

/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the POC box.
 * @param       header_size   the size of the data contained in the POC marker.

 */
static bool j2k_read_poc(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Gets the maximum size taken by the toc headers of all the tile parts of any given tile.
 */
static uint32_t j2k_get_max_toc_size(grk_j2k *p_j2k);

/**
 * Gets the maximum size taken by the headers of the SOT.
 *
 * @param       p_j2k   the jpeg2000 codec to use.
 */
static uint64_t j2k_get_specific_header_sizes(grk_j2k *p_j2k);

/**
 * Reads a CRG marker (Component registration)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the TLM box.
 * @param       header_size   the size of the data contained in the TLM marker.

 */
static bool j2k_read_crg(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the TLM box.
 * @param       header_size   the size of the data contained in the TLM marker.

 */
static bool j2k_read_tlm(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the updated tlm.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_updated_tlm(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the TLM box.
 * @param       header_size   the size of the data contained in the TLM marker.

 */
static bool j2k_read_plm(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the PLT box.
 * @param       header_size   the size of the data contained in the PLT marker.

 */
static bool j2k_read_plt(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Reads a PPM marker (Packed headers, main header)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the POC box.
 * @param       header_size   the size of the data contained in the POC marker.

 */

static bool j2k_read_ppm(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Merges all PPM markers read (Packed headers, main header)
 *
 * @param       p_cp      main coding parameters.

 */
static bool j2k_merge_ppm(grk_coding_parameters *p_cp);

/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the PPT box.
 * @param       header_size   the size of the data contained in the PPT marker.

 */
static bool j2k_read_ppt(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Merges all PPT markers read (Packed headers, tile-part header)
 *
 * @param       p_tcp   the tile.

 */
static bool j2k_merge_ppt(grk_tcp *p_tcp);

/**
 * Writes the TLM marker (Tile Length Marker)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_tlm(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes the SOT marker (Start of tile-part)
 *
 * @param       p_j2k            J2K codec.
 * @param       p_stream         the stream to write data to.
 * @param       psot_location    PSOT location
 * @param       p_data_written   number of bytes written

 */
static bool j2k_write_sot(grk_j2k *p_j2k, BufferedStream *p_stream,
		uint64_t *psot_location, uint64_t *p_data_written);

/**
 * Reads values from a SOT marker (Start of tile-part)
 *
 * the j2k decoder state is not affected. No side effects, no checks except for header_size.
 *
 * @param       p_header_data   the data contained in the SOT marker.
 * @param       header_size   the size of the data contained in the SOT marker.
 * @param       tile_no       Isot.
 * @param       p_tot_len       Psot.
 * @param       p_current_part  TPsot.
 * @param       p_num_parts     TNsot.

 */
static bool j2k_get_sot_values(uint8_t *p_header_data, uint32_t header_size,
		uint16_t *tile_no, uint32_t *p_tot_len, uint8_t *p_current_part,
		uint8_t *p_num_parts);
/**
 * Reads a SOT marker (Start of tile-part)
 *
 * @param       p_j2k           the jpeg2000 codec.
 * @param       p_header_data   the data contained in the SOT marker.
 * @param       header_size   the size of the data contained in the PPT marker.

 */
static bool j2k_read_sot(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Writes the SOD marker (Start of data)
 *
 * @param       p_j2k               J2K codec.
 * @param       p_tile_coder        tile coder
 * @param       p_data_written      number of bytes written
 * @param       total_data_size   total data size
 * @param       p_stream            the stream to write data to.

 */
static bool j2k_write_sod(grk_j2k *p_j2k, TileProcessor *p_tile_coder,
		uint64_t *p_data_written, uint64_t total_data_size,
		BufferedStream *p_stream);

/**
 * Reads a SOD marker (Start Of Data)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_stream                FIXME DOC

 */
static bool j2k_read_sod(grk_j2k *p_j2k, BufferedStream *p_stream);

static grk_tcp* j2k_get_tcp(grk_j2k *p_j2k) {
	auto l_cp = &(p_j2k->m_cp);
	return (j2k_fromTileHeader(p_j2k)) ?
			l_cp->tcps + p_j2k->m_current_tile_number :
			p_j2k->m_specific_param.m_decoder.m_default_tcp;
}

static void j2k_update_tlm(grk_j2k *p_j2k, uint32_t tile_part_size) {
	/* PSOT */
	grok_write_bytes(
			p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current,
			p_j2k->m_current_tile_number, 1);
	++p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current;

	/* PSOT */
	grok_write_bytes(
			p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current,
			tile_part_size, 4);
	p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current += 4;
}

/**
 * Writes the RGN marker (Region Of Interest)
 *
 * @param       tile_no               the tile to output
 * @param       comp_no               the component to output
 * @param       nb_comps                the number of components
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.

 */
static bool j2k_write_rgn(grk_j2k *p_j2k, uint16_t tile_no, uint32_t comp_no,
		uint32_t nb_comps, BufferedStream *p_stream);

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the POC box.
 * @param       header_size   the size of the data contained in the POC marker.

 */
static bool j2k_read_rgn(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the EOC marker (End of Codestream)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_eoc(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes the CBD-MCT-MCC-MCO markers (Multi components transform)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                        the stream to write data to.

 */
static bool j2k_write_mct_data_group(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Inits the Info
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_init_info(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 Add main header marker information
 @param cstr_index    Codestream information structure
 @param type         marker type
 @param pos          byte offset of marker segment
 @param len          length of marker segment
 */
static bool j2k_add_mhmarker( grk_codestream_index  *cstr_index, uint32_t type,
		uint64_t pos, uint32_t len);
/**
 Add tile header marker information
 @param tileno       tile index number
 @param cstr_index   Codestream information structure
 @param type         marker type
 @param pos          byte offset of marker segment
 @param len          length of marker segment
 */
static bool j2k_add_tlmarker(uint16_t tileno,
		 grk_codestream_index  *cstr_index, uint32_t type, uint64_t pos,
		uint32_t len);

/**
 * Reads an unknown marker
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_stream                the stream object to read from.
 * @param       output_marker           FIXME DOC

 *
 * @return      true                    if the marker could be deduced.
 */
static bool j2k_read_unk(grk_j2k *p_j2k, BufferedStream *p_stream,
		uint32_t *output_marker);

/**
 * Writes the MCT marker (Multiple Component Transform)
 *
 * @param       p_mct_record    FIXME DOC
 * @param       p_stream        the stream to write data to.

 */
static bool j2k_write_mct_record(grk_mct_data *p_mct_record, BufferedStream *p_stream);

/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the MCT box.
 * @param       header_size   the size of the data contained in the MCT marker.

 */
static bool j2k_read_mct(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the MCC marker (Multiple Component Collection)
 *
 * @param       p_mcc_record            FIXME DOC
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_mcc_record(grk_simple_mcc_decorrelation_data *p_mcc_record,
		BufferedStream *p_stream);

/**
 * Reads a MCC marker (Multiple Component Collection)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the MCC box.
 * @param       header_size   the size of the data contained in the MCC marker.

 */
static bool j2k_read_mcc(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the MCO marker (Multiple component transformation ordering)
 *
 * @param       p_j2k                           J2K codec.
 * @param       p_stream                                the stream to write data to.

 */
static bool j2k_write_mco(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the MCO box.
 * @param       header_size   the size of the data contained in the MCO marker.

 */
static bool j2k_read_mco(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

static bool j2k_add_mct(grk_tcp *p_tcp, grk_image *p_image, uint32_t index);

static void j2k_read_int16_to_float(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_read_int32_to_float(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_read_float32_to_float(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_read_float64_to_float(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);

static void j2k_read_int16_to_int32(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_read_int32_to_int32(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_read_float32_to_int32(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_read_float64_to_int32(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);

static void j2k_write_float_to_int16(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_write_float_to_int32(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_write_float_to_float(const void *p_src_data, void *p_dest_data,
		uint32_t nb_elem);
static void j2k_write_float_to_float64(const void *p_src_data,
		void *p_dest_data, uint32_t nb_elem);

/**
 * Ends the encoding, i.e. frees memory.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_end_encoding(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes the CBD marker (Component bit depth definition)
 *
 * @param       p_j2k                           J2K codec.
 * @param       p_stream                                the stream to write data to.

 */
static bool j2k_write_cbd(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Reads a CBD marker (Component bit depth definition)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_data   the data contained in the CBD box.
 * @param       header_size   the size of the data contained in the CBD marker.

 */
static bool j2k_read_cbd(grk_j2k *p_j2k, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes COC marker for each component.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_all_coc(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes QCC marker for each component.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_all_qcc(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes regions of interests.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_regions(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Writes EPC ????
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.

 */
static bool j2k_write_epc(grk_j2k *p_j2k, BufferedStream *p_stream);

/**
 * Checks the progression order changes values. Tells of the poc given as input are valid.
 * A nice message is outputted at errors.
 *
 * @param       p_pocs                  the progression order changes.
 * @param       nb_pocs               the number of progression order changes.
 * @param       nb_resolutions        the number of resolutions.
 * @param       numcomps                the number of components
 * @param       numlayers               the number of layers.

 *
 * @return      true if the pocs are valid.
 */
static bool j2k_check_poc_val(const  grk_poc  *p_pocs, uint32_t nb_pocs,
		uint32_t nb_resolutions, uint32_t numcomps, uint32_t numlayers);

/**
 * Gets the number of tile parts used for the given change of progression (if any) and the given tile.
 *
 * @param               cp                      the coding parameters.
 * @param               pino            the offset of the given poc (i.e. its position in the coding parameter).
 * @param               tileno          the given tile.
 *
 * @return              the number of tile parts.
 */
static uint32_t j2k_get_num_tp(grk_coding_parameters *cp, uint32_t pino, uint16_t tileno);

/**
 * Calculates the total number of tile parts needed by the encoder to
 * encode such an image. If not enough memory is available, then the function return false.
 *
 * @param       cp              coding parameters for the image.
 * @param       p_nb_tiles      total number of tile parts in whole image.
 * @param       image           image to encode.

 *
 * @return true if the function was successful, false else.
 */
static bool j2k_calculate_tp(grk_coding_parameters *cp, uint32_t *p_nb_tile_parts, grk_image *image);

static void j2k_dump_MH_info(grk_j2k *p_j2k, FILE *out_stream);

static void j2k_dump_MH_index(grk_j2k *p_j2k, FILE *out_stream);

static  grk_codestream_index  *  j2k_create_cstr_index(void);

static float j2k_get_tp_stride(grk_tcp *p_tcp);

static float j2k_get_default_stride(grk_tcp *p_tcp);

static uint32_t j2k_initialise_4K_poc( grk_poc  *POC, uint32_t numres);

static void j2k_set_cinema_parameters( grk_cparameters  *parameters,
		grk_image *image);

static bool j2k_is_cinema_compliant(grk_image *image, uint16_t rsiz);

/**
 * Checks for invalid number of tile-parts in SOT marker (TPsot==TNsot). See issue 254.
 *
 * @param       p_stream            the stream to read data from.
 * @param       tile_no             tile number we're looking for.
 * @param       p_correction_needed output value. if true, non conformant codestream needs TNsot correction.

 *
 * @return true if the function was successful, false else.
 */
static bool j2k_need_nb_tile_parts_correction(BufferedStream *p_stream,
		uint16_t tile_no, bool *p_correction_needed);
