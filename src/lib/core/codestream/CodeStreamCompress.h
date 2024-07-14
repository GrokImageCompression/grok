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
   CodeStreamCompress(BufferedStream* stream);
   virtual ~CodeStreamCompress();

   static char* convertProgressionOrder(GRK_PROG_ORDER prg_order);
   static uint16_t getPocSize(uint32_t num_components, uint32_t l_nb_poc);

   bool start(void);
   bool init(grk_cparameters* p_param, GrkImage* p_image);
   uint64_t compress(grk_plugin_tile* tile);

 private:
   bool init_header_writing(void);
   bool cacheEndOfHeader(void);
   bool end(void);
   bool writeTilePart(TileProcessor* tileProcessor);
   bool writeTileParts(TileProcessor* tileProcessor);
   bool updateRates(void);
   bool compressValidation(void);
   bool mct_validation(void);

   /**
	* Writes the SOC marker (Start Of Codestream)
	*
	*/
   bool write_soc();

   /**
	* Writes the SIZ marker (image and tile size)
	*
	*/
   bool write_siz();

   /**
	* Writes the CAP marker
	*
	*/
   bool write_cap();

   /**
	* Writes the COM marker (comment)
	*
	*/
   bool write_com();

   /**
	* Writes the COD marker (Coding style default)
	*
	*/
   bool write_cod();

   /**
	* Compares 2 COC markers (Coding style component)
	*
	* @param       first_comp_no  the index of the first component to compare.
	* @param       second_comp_no the index of the second component to compare.
	*
	* @return      true if equals
	*/
   bool compare_coc(uint32_t first_comp_no, uint32_t second_comp_no);

   /**
	* Writes the COC marker (Coding style component)
	*
	* @param       comp_no   the index of the component to output.
	* @param       stream    buffered stream.

	*/
   bool write_coc(uint32_t comp_no, BufferedStream* stream);

   bool write_coc(uint32_t comp_no);

   /**
	* Writes the QCD marker (quantization default)
	*
	*/
   bool write_qcd();

   /**
	* Compare QCC markers (quantization component)
	*
	* @param       first_comp_no       the index of the first component to compare.
	* @param       second_comp_no      the index of the second component to compare.
	*
	* @return true if equals.
	*/
   bool compare_qcc(uint32_t first_comp_no, uint32_t second_comp_no);

   /**
	* Writes the QCC marker (quantization component)
	*
	* @param 		tile_index 	current tile index
	* @param       comp_no     the index of the component to output.
	* @param       stream      buffered stream.

	*/
   bool write_qcc(uint16_t tile_index, uint32_t comp_no, BufferedStream* stream);

   bool write_qcc(uint32_t comp_no);

   /**
	* Writes the POC marker (Progression Order Change)
	*
	*/
   bool writePoc();

   /**
	* End writing the updated tlm.
	*
	*/
   bool write_tlm_end();

   /**
	* Begin writing the TLM marker (Tile Length Marker)
	*
	*/
   bool write_tlm_begin();

   /**
	* Compare 2 a SPCod/ SPCoc elements, i.e. the coding style of a given component of a tile.
	*
	* @param       first_comp_no  The 1st component number to compare.
	* @param       second_comp_no The 1st component number to compare.
	*
	* @return true if SPCdod are equals.
	*/
   bool compare_SPCod_SPCoc(uint32_t first_comp_no, uint32_t second_comp_no);

   /**
	* Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
	*
	* @param       comp_no       the component number to output.
	*
	* @return true if successful
	*/
   bool write_SPCod_SPCoc(uint32_t comp_no);

   /**
	* Gets the size taken by writing a SPCod or SPCoc for the given tile and component.
	*
	* @param       comp_no               the component being outputted.
	*
	* @return      the number of bytes taken by the SPCod element.
	*/
   uint32_t get_SPCod_SPCoc_size(uint32_t comp_no);

   /**
	* Gets the size taken by writing SQcd or SQcc element, i.e. the quantization values of a band
	* in the QCD or QCC.
	*
	* @param       comp_no               the component being output.
	*
	* @return      the number of bytes taken by the SPCod element.
	*/
   uint32_t get_SQcd_SQcc_size(uint32_t comp_no);

   /**
	* Compares 2 SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
	*
	* @param       first_comp_no         the first component number to compare.
	* @param       second_comp_no        the second component number to compare.
	*
	* @return true if equals.
	*/
   bool compare_SQcd_SQcc(uint32_t first_comp_no, uint32_t second_comp_no);

   /**
	* Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
	*
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
   bool write_mct_record(grk_mct_data* p_mct_record, BufferedStream* stream);

   /**
	* Writes the MCC marker (Multiple Component Collection)
	*
	* @param       p_mcc_record          MCC record
	* @param       stream                buffered stream.

	*/
   bool write_mcc_record(grk_simple_mcc_decorrelation_data* p_mcc_record, BufferedStream* stream);

   /**
	* Writes the MCO marker (Multiple component transformation ordering)
	*
	*/
   bool write_mco();

   /**
	* Writes the CBD marker (Component bit depth definition)
	*
	*/
   bool write_cbd();

   /**
	* Writes COC marker for each component.
	*
	*/
   bool write_all_coc();

   /**
	* Writes QCC marker for each component.
	*
	*/
   bool write_all_qcc();

   /**
	* Writes regions of interests.
	*
	*/
   bool write_regions();

   /**
	* Writes the RGN marker (Region Of Interest)
	*
	* @param       tile_no               the tile to output
	* @param       comp_no               the component to output
	* @param       nb_comps                the number of components
	*
	*/
   bool write_rgn(uint16_t tile_no, uint32_t comp_no, uint32_t nb_comps);

   /**
	* Writes the EOC marker (End of Codestream)
	*
	*/
   bool write_eoc();

   /**
	* Writes the CBD-MCT-MCC-MCO markers (Multi components transform)
	*
	*/
   bool write_mct_data_group();

   /**
	  * Calculates the total number of tile parts needed by the compressor to
	  * compress such an image. If not enough memory is available, then the function return false.
	  *
	  * @param       p_nb_tile_parts total number of tile parts in whole image.
	  * @param       image           image to compress.

	  *
	  * @return true if the function was successful, false else.
	  */
   bool getNumTileParts(uint16_t* p_nb_tile_parts, GrkImage* image);

   /**
	* Gets the number of tile parts used for the given change of progression (if any) and the given
	* tile.
	*
	* @param               pino            the offset of the given poc (i.e. its position in the
	* coding parameter).
	* @param               tileno          the given tile.
	*
	* @return              the number of tile parts.
	*/
   uint64_t getNumTilePartsForProgression(uint32_t pino, uint16_t tileno);

   /**
	* Validate progression orders
	*
	* @param       progressions          progression orders.
	* @param       numProgressions       number of progression orders.
	* @param       num_resolutions        number of resolutions.
	* @param       numcomps              number of components
	* @param       numlayers             number of layers.
	*
	* @return      true if the pocs are valid.
	*/
   bool validateProgressionOrders(const grk_progression* progressions, uint32_t numProgressions,
								  uint8_t num_resolutions, uint16_t numcomps, uint16_t numlayers);

   bool init_mct_encoding(TileCodingParams* p_tcp, GrkImage* p_image);

   CompressorState compressorState_;
};

} // namespace grk
