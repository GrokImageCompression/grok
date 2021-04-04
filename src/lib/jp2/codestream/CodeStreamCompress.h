/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

namespace grk
{
class CodeStream;

class CodeStreamCompress : public CodeStream, public ICodeStreamCompress
{
  public:
	CodeStreamCompress(IBufferedStream* stream);
	virtual ~CodeStreamCompress();

	static char* convertProgressionOrder(GRK_PROG_ORDER prg_order);

	bool startCompress(void);
	bool initCompress(grk_cparameters* p_param, GrkImage* p_image);
	bool compress(grk_plugin_tile* tile);
	bool compressTile(uint16_t tileIndex, uint8_t* p_data, uint64_t data_size);
	bool endCompress(void);

  private:
	bool init_header_writing(void);
	bool get_end_header(void);
	bool writeTilePart(TileProcessor* tileProcessor);
	bool writeTileParts(TileProcessor* tileProcessor);
	bool update_rates(void);
	bool compress_validation(void);
	bool mct_validation(void);

	/**
	 * Writes the SOC marker (Start Of Codestream)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_soc();

	/**
	 * Writes the SIZ marker (image and tile size)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_siz();

	/**
	 * Writes the CAP marker
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_cap();

	/**
	 * Writes the COM marker (comment)
	 *
	 * @param       codeStream      JPEG 2000 code stream
	 */
	bool write_com();

	/**
	 * Writes the COD marker (Coding style default)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_cod();

	/**
	 * Compares 2 COC markers (Coding style component)
	 *
	 * @param       codeStream            JPEG 2000 code stream
	 * @param       first_comp_no  the index of the first component to compare.
	 * @param       second_comp_no the index of the second component to compare.
	 *
	 * @return      true if equals
	 */
	bool compare_coc(uint32_t first_comp_no, uint32_t second_comp_no);

	/**
	 * Writes the COC marker (Coding style component)
	 *
	 * @param       codeStream  JPEG 2000 code stream
	 * @param       comp_no   the index of the component to output.
	 * @param       stream    buffered stream.

	 */
	bool write_coc(uint32_t comp_no, IBufferedStream* stream);

	bool write_coc(uint32_t comp_no);

	/**
	 * Writes the QCD marker (quantization default)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_qcd();

	/**
	 * Compare QCC markers (quantization component)
	 *
	 * @param       codeStream                 JPEG 2000 code stream
	 * @param       first_comp_no       the index of the first component to compare.
	 * @param       second_comp_no      the index of the second component to compare.
	 *
	 * @return true if equals.
	 */
	bool compare_qcc(uint32_t first_comp_no, uint32_t second_comp_no);

	/**
	 * Writes the QCC marker (quantization component)
	 *
	 * @param       codeStream  JPEG 2000 code stream
	 * @param 		tileIndex 	current tile index
	 * @param       comp_no     the index of the component to output.
	 * @param       stream      buffered stream.

	 */
	bool write_qcc(uint16_t tileIndex, uint32_t comp_no, IBufferedStream* stream);

	bool write_qcc(uint32_t comp_no);

	uint16_t getPocSize(uint32_t numComponents, uint32_t l_nb_poc);

	/**
	 * Writes the POC marker (Progression Order Change)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_poc();

	/**
	 * End writing the updated tlm.
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_tlm_end();

	/**
	 * Begin writing the TLM marker (Tile Length Marker)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_tlm_begin();

	/**
	 * Compare 2 a SPCod/ SPCoc elements, i.e. the coding style of a given component of a tile.
	 *
	 * @param       codeStream            JPEG 2000 code stream
	 * @param       first_comp_no  The 1st component number to compare.
	 * @param       second_comp_no The 1st component number to compare.
	 *
	 * @return true if SPCdod are equals.
	 */
	bool compare_SPCod_SPCoc(uint32_t first_comp_no, uint32_t second_comp_no);

	/**
	 * Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
	 *
	 * @param       codeStream           JPEG 2000 code stream
	 * @param       comp_no       the component number to output.
	 *
	 * @return true if successful
	 */
	bool write_SPCod_SPCoc(uint32_t comp_no);

	/**
	 * Gets the size taken by writing a SPCod or SPCoc for the given tile and component.
	 *
	 * @param       codeStream                   the JPEG 2000 code stream
	 * @param       comp_no               the component being outputted.
	 *
	 * @return      the number of bytes taken by the SPCod element.
	 */
	uint32_t get_SPCod_SPCoc_size(uint32_t comp_no);

	/**
	 * Gets the size taken by writing SQcd or SQcc element, i.e. the quantization values of a band
	 * in the QCD or QCC.
	 *
	 * @param       codeStream                   the JPEG 2000 code stream
	 * @param       comp_no               the component being output.
	 *
	 * @return      the number of bytes taken by the SPCod element.
	 */
	uint32_t get_SQcd_SQcc_size(uint32_t comp_no);

	/**
	 * Compares 2 SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
	 *
	 * @param       codeStream                   JPEG 2000 code stream
	 * @param       first_comp_no         the first component number to compare.
	 * @param       second_comp_no        the second component number to compare.
	 *
	 * @return true if equals.
	 */
	bool compare_SQcd_SQcc(uint32_t first_comp_no, uint32_t second_comp_no);

	/**
	 * Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
	 *
	 * @param       codeStream                   JPEG 2000 code stream
	 * @param       comp_no               the component number to output.
	 *
	 */
	bool write_SQcd_SQcc(uint32_t comp_no);

	/**
	 * Writes the MCT marker (Multiple Component Transform)
	 *
	 * @param       p_mct_record  MCT record
	 * @param       stream        buffered stream.

	 */
	bool write_mct_record(grk_mct_data* p_mct_record, IBufferedStream* stream);

	/**
	 * Writes the MCC marker (Multiple Component Collection)
	 *
	 * @param       p_mcc_record          MCC record
	 * @param       stream                buffered stream.

	 */
	bool write_mcc_record(grk_simple_mcc_decorrelation_data* p_mcc_record, IBufferedStream* stream);

	/**
	 * Writes the MCO marker (Multiple component transformation ordering)
	 *
	 * @param       codeStream      JPEG 2000 code stream
	 */
	bool write_mco();

	/**
	 * Writes the CBD marker (Component bit depth definition)
	 *
	 * @param       codeStream                           JPEG 2000 code stream
	 */
	bool write_cbd();

	/**
	 * Writes COC marker for each component.
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_all_coc();

	/**
	 * Writes QCC marker for each component.
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_all_qcc();

	/**
	 * Writes regions of interests.
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_regions();

	/**
	 * Writes the RGN marker (Region Of Interest)
	 *
	 * @param       tile_no               the tile to output
	 * @param       comp_no               the component to output
	 * @param       nb_comps                the number of components
	 * @param       codeStream                   JPEG 2000 code stream

	 */
	bool write_rgn(uint16_t tile_no, uint32_t comp_no, uint32_t nb_comps);

	/**
	 * Writes the EOC marker (End of Codestream)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_eoc();

	/**
	 * Writes the CBD-MCT-MCC-MCO markers (Multi components transform)
	 *
	 * @param       codeStream          JPEG 2000 code stream
	 */
	bool write_mct_data_group();

	/**
	   * Calculates the total number of tile parts needed by the compressor to
	   * compress such an image. If not enough memory is available, then the function return false.
	   *
	   * @param       cp              coding parameters for the image.
	   * @param       p_nb_tile_parts total number of tile parts in whole image.
	   * @param       image           image to compress.

	   *
	   * @return true if the function was successful, false else.
	   */
	bool calculate_tp(CodingParams* cp, uint16_t* p_nb_tile_parts, GrkImage* image);

	/**
	 * Gets the number of tile parts used for the given change of progression (if any) and the given
	 * tile.
	 *
	 * @param               cp                      the coding parameters.
	 * @param               pino            the offset of the given poc (i.e. its position in the
	 * coding parameter).
	 * @param               tileno          the given tile.
	 *
	 * @return              the number of tile parts.
	 */
	uint64_t get_num_tp(CodingParams* cp, uint32_t pino, uint16_t tileno);

	/**
	 * Checks the progression order changes values. Tells of the poc given as input are valid.
	 *
	 * @param       p_pocs                the progression order changes.
	 * @param       nb_pocs               the number of progression order changes.
	 * @param       nb_resolutions        the number of resolutions.
	 * @param       numcomps              the number of components
	 * @param       numlayers             the number of layers.
	 *
	 * @return      true if the pocs are valid.
	 */
	bool check_poc_val(const grk_progression* p_pocs, uint32_t nb_pocs, uint32_t nb_resolutions,
					   uint32_t numcomps, uint16_t numlayers);

	bool init_mct_encoding(TileCodingParams* p_tcp, GrkImage* p_image);

	CompressorState m_compressorState;
};

} // namespace grk
