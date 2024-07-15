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
typedef std::function<bool(uint8_t* headerData, uint16_t header_size)> MARKER_FUNC;
struct marker_handler
{
   marker_handler(uint16_t ID, uint32_t flags, MARKER_FUNC f) : id(ID), states(flags), func(f) {}
   /** marker value */
   uint16_t id;
   /** value of the state when the marker can appear */
   uint32_t states;
   MARKER_FUNC func;
};

class CodeStreamDecompress : public CodeStream, public ICodeStreamDecompress
{
 public:
   CodeStreamDecompress(BufferedStream* stream);
   virtual ~CodeStreamDecompress();
   TileProcessor* allocateProcessor(uint16_t tile_index);
   DecompressorState* getDecompressorState(void);
   TileCodingParams* get_current_decode_tcp(void);
   bool isDecodingTilePartHeader();
   bool readHeader(grk_header_info* header_info);
   GrkImage* getImage(uint16_t tile_index);
   GrkImage* getImage(void);
   std::vector<GrkImage*> getAllImages(void);
   void init(grk_decompress_core_params* p_param);
   bool setDecompressRegion(grk_rect_double region);
   bool decompress(grk_plugin_tile* tile);
   bool decompressTile(uint16_t tile_index);
   bool preProcess(void);
   bool postProcess(void);
   CodeStreamInfo* getCodeStreamInfo(void);
   GrkImage* getCompositeImage();
   bool readMarker(void);
   bool readMarker(bool suppressWarning);
   GrkImage* getHeaderImage(void);
   uint16_t getCurrentMarker(void);
   void dump(uint32_t flag, FILE* outputFileStream);
   bool needsHeaderRead(void);
   void setExpectSOD();

 protected:
   void dump_MH_info(FILE* outputFileStream);
   /**
	* Dump an image header structure.
	*
	*@param image			the image header to dump.
	*@param dev_dump_flag		flag to describe if we are in the case of this function is use
	*outside dump function
	*@param outputFileStream			output stream where dump the elements.
	*/
   void dump_image_header(GrkImage* image, bool dev_dump_flag, FILE* outputFileStream);
   void dump_tile_info(TileCodingParams* default_tile, uint32_t numcomps, FILE* outputFileStream);
   /**
	* Dump a component image header structure.
	*
	*@param comp		the component image header to dump.
	*@param dev_dump_flag		flag to describe if we are in the case of this function is use
	*outside dump function
	*@param outputFileStream			output stream where dump the elements.
	*/
   void dump_image_comp_header(grk_image_comp* comp, bool dev_dump_flag, FILE* outputFileStream);

 private:
   bool readCurrentMarkerBody(uint16_t* markerSize);
   bool endOfCodeStream(void);
   bool read_short(uint16_t* val);
   bool process_marker(const marker_handler* marker_handler, uint16_t marker_size);
   bool readSOTorEOC(void);
   bool parseTileParts(bool* can_decode_tile_data);
   bool readHeaderProcedureImpl(void);
   bool decompressExec();
   bool decompressTile();
   bool findNextSOT(TileProcessor* tileProcessor);
   bool skipNonScheduledTLM(CodingParams* cp);
   bool hasTLM(void);
   void nextTLM(void);
   bool decompressTiles(void);
   bool decompressValidation(void);
   bool copy_default_tcp(void);
   bool read_unk(void);
   /**
	Add main header marker information
	@param id           marker id
	@param pos          byte offset of marker segment
	@param len          length of marker segment
	*/
   void addMarker(uint16_t id, uint64_t pos, uint32_t len);
   /**
	* Reads a MCT marker (Multiple Component Transform)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_mct(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a MCC marker (Multiple Component Collection)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_mcc(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a MCO marker (Multiple Component Transform Ordering)
	*
	* @param       headerData   header data.
	* @param       header_size     size of header data
	*
	*/
   bool read_mco(uint8_t* headerData, uint16_t header_size);
   bool add_mct(TileCodingParams* p_tcp, GrkImage* p_image, uint32_t index);
   /**
	* Reads a CBD marker (Component bit depth definition)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_cbd(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a RGN marker (Region Of Interest)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*/
   bool read_rgn(uint8_t* headerData, uint16_t header_size);

   bool readHeaderProcedure(void);
   /**
	* Reads a SOC marker (Start of Codestream)
	*/
   bool read_soc();
   /**
	* Reads a SIZ marker (image and tile size)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*/
   bool read_siz(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a CAP marker
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*/
   bool read_cap(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a COM marker (comments)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_com(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a COD marker (Coding Style defaults)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_cod(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a POC marker (Progression Order Change)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*/
   bool read_poc(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a CRG marker (Component registration)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_crg(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a TLM marker (Tile Length Marker)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*/
   bool read_tlm(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a PLM marker (Packet length, main header marker)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_plm(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a PLT marker (Packet length, tile-part header)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_plt(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a PPM marker (Packed headers, main header)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_ppm(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a PPT marker (Packed packet headers, tile-part header)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_ppt(uint8_t* headerData, uint16_t header_size);
   /**
	* Read SOT (Start of tile part) marker
	*
	* @param       headerData   header data.
	* @param       header_size     size of header data
	*
	*/
   bool read_sot(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
	* @param       compno          component number
	* @param       headerData   the data contained in the COM box.
	* @param       header_size   the size of the data contained in the COM marker.

	*/
   bool read_SPCod_SPCoc(uint16_t compno, uint8_t* headerData, uint16_t* header_size);
   /**
	* Reads a SQcd or SQcc element, i.e. the quantization values of a band
	* in the QCD or QCC.
	*
	* @param		fromQCC			true if reading QCC, otherwise false (reading QCD)
	* @param       compno          the component number to output.
	* @param       headerData   the data buffer.
	* @param       header_size   pointer to the size of the data buffer,
	*              it is changed by the function.
	*
	*/
   bool read_SQcd_SQcc(bool fromQCC, uint16_t compno, uint8_t* headerData, uint16_t* header_size);
   /**
	* Merges all PPM markers read (Packed headers, main header)
	*
	* @param       p_cp      main coding parameters.

	*/
   bool merge_ppm(CodingParams* p_cp);
   /**
	* Merges all PPT markers read (Packed headers, tile-part header)
	*
	* @param       p_tcp   the tile.

	*/
   bool merge_ppt(TileCodingParams* p_tcp);
   /**
	* Reads a COC marker (Coding Style Component)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*
	*/
   bool read_coc(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a QCD marker (Quantization defaults)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data
	*/
   bool read_qcd(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads a QCC marker (Quantization component)
	*
	* @param       headerData   header data
	* @param       header_size     size of header data

	*/
   bool read_qcc(uint8_t* headerData, uint16_t header_size);
   /**
	* Reads the lookup table containing all the marker, status and action,
	* and returns the handler associated with the marker value.
	* @param       id            Marker value to look up
	*
	* @return      the handler associated with the id.
	*/
   const marker_handler* get_marker_handler(uint16_t id);

   bool createOutputImage(void);
   bool checkForIllegalTilePart(void);

   std::map<uint16_t, marker_handler*> marker_map;
   DecompressorState decompressorState_;
   bool expectSOD_;
   uint16_t curr_marker_;
   bool headerError_;
   bool headerRead_;
   uint8_t* marker_scratch_;
   uint16_t marker_scratch_size_;
   GrkImage* outputImage_;
   TileCache* tileCache_;
   StripCache stripCache_;
   grk_io_pixels_callback ioBufferCallback;
   void* ioUserData;
   grk_io_register_reclaim_callback grkRegisterReclaimCallback_;
};

} // namespace grk
