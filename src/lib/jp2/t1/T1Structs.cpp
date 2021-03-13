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
 */

#include "grk_includes.h"

namespace grk {


Segment::Segment() {
	clear();
}
void Segment::clear() {
	dataindex = 0;
	numpasses = 0;
	len = 0;
	maxpasses = 0;
	numPassesInPacket = 0;
	numBytesInPacket = 0;
}

PacketLengthInfo::PacketLengthInfo(uint32_t mylength, uint32_t bits) :
		len(mylength), len_bits(bits) {
}
PacketLengthInfo::PacketLengthInfo() :
		len(0), len_bits(0) {
}
bool PacketLengthInfo::operator==(const PacketLengthInfo &rhs) const {
	return (rhs.len == len && rhs.len_bits == len_bits);
}

CodePass::CodePass() :
		rate(0), distortiondec(0), len(0), term(0), slope(0) {
}
Layer::Layer() :
		numpasses(0), len(0), disto(0), data(nullptr) {
}

Precinct::Precinct(const grkRectU32 &bounds, bool isCompressor, grkPointU32 cblk_expn) : grkRectU32(bounds),
		precinctIndex(0),
		impl(new PrecinctImpl(isCompressor, this, cblk_expn)),
		m_cblk_expn(cblk_expn)
{
}
Precinct::~Precinct(){
	delete impl;
}

void Precinct::deleteTagTrees() {
	impl->deleteTagTrees();
}
grkRectU32 Precinct::getCodeBlockBounds(uint64_t cblkno){
	return impl->getCodeBlockBounds(cblkno);
}

void Precinct::initTagTrees() {
	impl->initTagTrees();
}
TagTree* Precinct::getInclTree(void){
	return  impl->incltree;
}
TagTree* Precinct::getImsbTree(void){
	return  impl->imsbtree;
}

uint32_t Precinct::getCblkGridwidth(void){
	return impl->m_cblk_grid.width();
}
uint32_t Precinct::getCblkGridHeight(void){
	return impl->m_cblk_grid.height();
}

uint32_t Precinct::getNominalBlockSize(void){
	return (1U << impl->m_cblk_expn.x) * (1U << impl->m_cblk_expn.y);
}

PrecinctImpl* Precinct::getImpl(void){
	impl->initCodeBlocks(this);
	return impl;
}
uint64_t Precinct::getNumCblks(void){
	return impl->m_cblk_grid.area();
}
CompressCodeblock* Precinct::getCompressedBlockPtr(uint64_t cblkno){
	return getImpl()->enc->get(cblkno);
}
DecompressCodeblock* Precinct::getDecompressedBlockPtr(uint64_t cblkno){
	return  getImpl()->dec->get(cblkno);
}
grkPointU32 Precinct::getCblkExpn(void){
	return m_cblk_expn;
}
grkRectU32 Precinct::getCblkGrid(void){
	return impl->m_cblk_grid;
}
PrecinctImpl::PrecinctImpl(bool isCompressor, grkRectU32 *bounds,grkPointU32 cblk_expn) :
		enc(nullptr),
		dec(nullptr),
		incltree(nullptr),
		imsbtree(nullptr),
		m_bounds(*bounds),
		m_cblk_expn(cblk_expn),
		m_isCompressor(isCompressor)
{
	m_cblk_grid = grkRectU32(floordivpow2(bounds->x0,cblk_expn.x),
									floordivpow2(bounds->y0,cblk_expn.y),
									ceildivpow2<uint32_t>(bounds->x1,cblk_expn.x),
									ceildivpow2<uint32_t>(bounds->y1,cblk_expn.y));
}
PrecinctImpl::~PrecinctImpl(){
	deleteTagTrees();
	delete enc;
	delete dec;
}
grkRectU32 PrecinctImpl::getCodeBlockBounds(uint64_t cblkno){
	auto cblk_start = grkPointU32(	(m_cblk_grid.x0  + (uint32_t) (cblkno % m_cblk_grid.width())) << m_cblk_expn.x,
								(m_cblk_grid.y0  + (uint32_t) (cblkno / m_cblk_grid.width())) << m_cblk_expn.y);
	auto cblk_bounds = grkRectU32(cblk_start.x,
									cblk_start.y,
									cblk_start.x + (1U << m_cblk_expn.x),
									cblk_start.y + (1U << m_cblk_expn.y));
	return  cblk_bounds.intersection(&m_bounds);
}
bool PrecinctImpl::initCodeBlocks(grkRectU32 *bounds){
	if ((m_isCompressor && enc) || (!m_isCompressor && dec))
		return true;
	m_bounds = *bounds;
	auto numBlocks = m_cblk_grid.area();
	if (!numBlocks)
		return true;
	if (m_isCompressor)
		enc =  new ChunkedArray<CompressCodeblock, PrecinctImpl>(this,numBlocks);
	else
		dec =  new ChunkedArray<DecompressCodeblock, PrecinctImpl>(this,numBlocks);
	initTagTrees();

	return true;
}
template<typename T> bool PrecinctImpl::initCodeBlock(T* block, uint64_t cblkno){
	if (block->non_empty())
		return true;
	if (!block->alloc())
		return false;
	block->setRect(getCodeBlockBounds(cblkno));

	return true;
}
void PrecinctImpl::deleteTagTrees() {
	delete incltree;
	incltree = nullptr;
	delete imsbtree;
	imsbtree = nullptr;
}
void PrecinctImpl::initTagTrees() {
	// if cw == 0 or ch == 0,
	// then the precinct has no code blocks, therefore
	// no need for inclusion and msb tag trees
	auto grid_width = m_cblk_grid.width();
	auto grid_height = m_cblk_grid.height();
	if (grid_width > 0 && grid_height > 0) {
		if (!incltree) {
			try {
				incltree = new TagTree(grid_width, grid_height);
			} catch (std::exception &e) {
				GRK_WARN("No incltree created.");
			}
		} else {
			if (!incltree->init(grid_width, grid_height)) {
				GRK_WARN("Failed to re-initialize incltree.");
				delete incltree;
				incltree = nullptr;
			}
		}

		if (!imsbtree) {
			try {
				imsbtree = new TagTree(grid_width, grid_height);
			} catch (std::exception &e) {
				GRK_WARN("No imsbtree created.");
			}
		} else {
			if (!imsbtree->init(grid_width, grid_height)) {
				GRK_WARN("Failed to re-initialize imsbtree.");
				delete imsbtree;
				imsbtree = nullptr;
			}
		}
	}
}
Codeblock::Codeblock():
		numbps(0),
		numlenbits(0),
		numPassesInPacket(0)
#ifdef DEBUG_LOSSLESS_T2
		,included(false)
#endif
		,m_failed(false)
{}
Codeblock::Codeblock(const Codeblock &rhs): grkRectU32(rhs),
											numbps(rhs.numbps),
											numlenbits(rhs.numlenbits),
											numPassesInPacket(rhs.numPassesInPacket),
											m_failed(rhs.m_failed)
#ifdef DEBUG_LOSSLESS_T2
	,included(0)
#endif
{
	compressedStream = rhs.compressedStream;
}
Codeblock& Codeblock::operator=(const Codeblock& rhs){
	if (this != &rhs) { // self-assignment check expected
		x0 = rhs.x0;
		y0 = rhs.y0;
		x1 = rhs.x1;
		y1 = rhs.y1;
		compressedStream = rhs.compressedStream;
		numbps = rhs.numbps;
		numlenbits = rhs.numlenbits;
		numPassesInPacket = rhs.numPassesInPacket;
#ifdef DEBUG_LOSSLESS_T2
		included = rhs.included;
		packet_length_info = rhs.packet_length_info;
#endif
	}
	return *this;
}
void Codeblock::setRect(grkRectU32 r){
	(*(grkRectU32*)this) = r;
	uncompressedData = grkBuffer2d<int32_t, AllocatorAligned>(width(),height());
}
bool Codeblock::allocUncompressedData(bool clear){
	return uncompressedData.alloc(clear);
}
int32_t* Codeblock::getUncomressedDataPtr(void){
	return uncompressedData.currPtr();
}
CompressCodeblock::CompressCodeblock() :
				paddedCompressedStream(nullptr),
				layers(	nullptr),
				passes(nullptr),
				numPassesInPreviousPackets(0),
				numPassesTotal(0),
				contextStream(nullptr)
{}
CompressCodeblock::~CompressCodeblock() {
	compressedStream.dealloc();
	grk_free(layers);
	grk_free(passes);
}
bool CompressCodeblock::alloc() {
	if (!layers) {
		layers = (Layer*) grk_calloc(100, sizeof(Layer));
		if (!layers)
			return false;
	}
	if (!passes) {
		passes = (CodePass*) grk_calloc(100, sizeof(CodePass));
		if (!passes)
			return false;
	}

	return true;
}
/**
 * Allocates data memory for an compressing code block.
 * We actually allocate 2 more bytes than specified, and then offset data by +2.
 * This is done so that we can safely initialize the MQ coder pointer to data-1,
 * without risk of accessing uninitialized memory.
 */
bool CompressCodeblock::allocData(size_t nominalBlockSize) {
	uint32_t desired_data_size = (uint32_t) (nominalBlockSize * sizeof(uint32_t));
	// we add two fake zero bytes at beginning of buffer, so that mq coder
	//can be initialized to data[-1] == actualData[1], and still point
	//to a valid memory location
	auto buf =  new uint8_t[desired_data_size + grk_cblk_enc_compressed_data_pad_left];
	buf[0] = 0;
	buf[1] = 0;

	paddedCompressedStream = buf + grk_cblk_enc_compressed_data_pad_left;
	compressedStream.buf = buf;
	compressedStream.len = desired_data_size;
	compressedStream.owns_data = true;

	return true;
}
DecompressCodeblock::DecompressCodeblock() : 	segs(nullptr),
												numSegments(0),
												#ifdef DEBUG_LOSSLESS_T2
												included(0),
												#endif
												numSegmentsAllocated(0)
{}
DecompressCodeblock::~DecompressCodeblock(){
	compressedStream.dealloc();
	cleanup_seg_buffers();
	delete[] segs;
}
Segment* DecompressCodeblock::getSegment(uint32_t segmentIndex){
	if (!segs) {
		numSegmentsAllocated = 1;
		segs = new Segment[numSegmentsAllocated];
		numSegmentsAllocated = 1;
	} else if (segmentIndex >= numSegmentsAllocated){
		auto new_segs = new Segment[2*numSegmentsAllocated];
		for (uint32_t i = 0; i < numSegmentsAllocated; ++i)
			new_segs[i] = segs[i];
		numSegmentsAllocated *= 2;;
		delete[] segs;
		segs = new_segs;
	}

	return segs + segmentIndex;
}
bool DecompressCodeblock::alloc(){
	return true;
}
uint32_t  DecompressCodeblock::getNumSegments(void){
	return numSegments;
}
Segment* DecompressCodeblock::getCurrentSegment(void){
	return numSegments ? getSegment(numSegments-1) : nullptr;
}
Segment* DecompressCodeblock::nextSegment(void){
	numSegments++;
	return getCurrentSegment();
}
void DecompressCodeblock::cleanup_seg_buffers(){
	for (auto& b : seg_buffers)
		delete b;
	seg_buffers.clear();
	numSegments = 0;
}
size_t DecompressCodeblock::getSegBuffersLen(){
	return std::accumulate(seg_buffers.begin(), seg_buffers.end(), (size_t)0, [](const size_t s, grkBufferU8 *a){
	   return (s + a->len);
	});
}
bool DecompressCodeblock::copy_to_contiguous_buffer(uint8_t *buffer) {
	if (!buffer)
		return false;
	size_t offset = 0;
	for (auto& buf : seg_buffers){
		if (buf->len){
			memcpy(buffer + offset, buf->buf, buf->len);
			offset += buf->len;
		}
	}
	return true;
}
bool DecompressCodeblock::needsDecompress(void){
	return !m_failed && !getUncomressedDataPtr();
}
Subband::Subband() :
				orientation(BAND_ORIENT_LL),
				numPrecincts(0),
				numbps(0),
				stepsize(0) {
}
//note: don't copy precinct array
Subband::Subband(const Subband &rhs) : grkRectU32(rhs),
										orientation(rhs.orientation),
										numPrecincts(0),
										numbps(rhs.numbps),
										stepsize(rhs.stepsize)
{}
Subband& Subband::operator= (const Subband &rhs){
	if (this != &rhs) { // self-assignment check expected
		*this = Subband(rhs);
	}
	return *this;
}
void Subband::print(){
	grkRectU32::print();
}
bool Subband::isEmpty() {
	return ((x1 - x0 == 0) || (y1 - y0 == 0));
}
Precinct* Subband::getPrecinct(uint64_t precinctIndex){
	if (precinctMap.find(precinctIndex) == precinctMap.end())
		return nullptr;
	uint64_t index = precinctMap[precinctIndex];

	return precincts[index];
}
grkRectU32 Subband::generatePrecinctBounds(uint64_t precinctIndex,
												grkPointU32 precinctRegionStart,
												grkPointU32 precinct_expn,
												uint32_t precinctGridWidth){
	auto precinctStart = grkPointU32(	precinctRegionStart.x + (uint32_t)((precinctIndex % precinctGridWidth) << precinct_expn.x),
			precinctRegionStart.y + (uint32_t)((precinctIndex / precinctGridWidth) << precinct_expn.y));
	return grkRectU32(precinctStart.x,
						precinctStart.y,
						precinctStart.x + (1U << precinct_expn.x),
						precinctStart.y + (1U << precinct_expn.y)).intersection(this);
}
Precinct* Subband::createPrecinct(bool isCompressor,
									uint64_t precinctIndex,
									grkPointU32 precinctRegionStart,
									grkPointU32 precinct_expn,
									uint32_t precinctGridWidth,
									grkPointU32 cblk_expn){
	auto temp = precinctMap.find(precinctIndex);
	if (temp != precinctMap.end())
		return precincts[temp->second];

	auto bandPrecinctBounds = generatePrecinctBounds(precinctIndex,precinctRegionStart,precinct_expn,precinctGridWidth);
	auto currPrec = new Precinct(bandPrecinctBounds, isCompressor,cblk_expn);
	currPrec->precinctIndex = precinctIndex;
	precincts.push_back(currPrec);
	precinctMap[precinctIndex] = precincts.size()-1;

	return currPrec;
}
Resolution::Resolution() :
		initialized(false),
		numTileBandWindows(0),
		precinctGridWidth(0),
		precinctGridHeight(0),
		current_plugin_tile(nullptr)
{}
void Resolution::print(){
	grkRectU32::print();
	for (uint32_t i = 0; i < numTileBandWindows; ++i){
		std::cout << "band " << i << " : ";
		tileBand[i].print();
	}
}
bool Resolution::init(bool isCompressor,
						TileComponentCodingParams *tccp,
						uint8_t resno,
						grk_plugin_tile *current_plugin_tile){
	if (initialized)
		return true;

	this->current_plugin_tile = current_plugin_tile;

	/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
	precinctExpn = grkPointU32(tccp->precinctWidthExp[resno],tccp->precinctHeightExp[resno]);

	/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
	precinctStart = grkPointU32(floordivpow2(x0, precinctExpn.x) << precinctExpn.x,
							floordivpow2(y0, precinctExpn.y) << precinctExpn.y);

	uint64_t num_precincts = (uint64_t)precinctGridWidth * precinctGridHeight;
	if (resno != 0) {
		precinctStart=  grkPointU32(ceildivpow2<uint32_t>(precinctStart.x, 1),
								ceildivpow2<uint32_t>(precinctStart.y, 1));
		precinctExpn.x--;
		precinctExpn.y--;
	}
	cblkExpn    =  grkPointU32(std::min<uint32_t>(tccp->cblkw, precinctExpn.x),
						   std::min<uint32_t>(tccp->cblkh, precinctExpn.y));
	for (uint8_t bandIndex = 0; bandIndex < numTileBandWindows; ++bandIndex) {
		auto curr_band = tileBand + bandIndex;
		curr_band->numPrecincts = num_precincts;
		if (isCompressor) {
			for (uint64_t precinctIndex = 0; precinctIndex < num_precincts; ++precinctIndex) {
				if (!curr_band->createPrecinct(true,
									precinctIndex,
									precinctStart,
									precinctExpn,
									precinctGridWidth,
									cblkExpn))
					return false;

			}
		}
	}
	initialized = true;

	return true;
}
BlockExec::BlockExec() : 	tilec(nullptr),
							bandIndex(0),
							bandOrientation(BAND_ORIENT_LL),
							stepsize(0),
							cblk_sty(0),
							qmfbid(0),
							x(0),
							y(0),
							k_msbs(0),
							isOpen(false)
{}
CompressBlockExec::CompressBlockExec() :
		            cblk(nullptr),
					tile(nullptr),
					doRateControl(false),
					distortion(0),
					tiledp(nullptr),
					compno(0),
					resno(0),
					precinctIndex(0),
					cblkno(0),
					inv_step_ht(0),
					mct_norms(nullptr),
#ifdef DEBUG_LOSSLESS_T1
	unencodedData(nullptr),
#endif
				mct_numcomps(0)
{}
bool CompressBlockExec::open(T1Interface *t1){
	isOpen = t1->compress(this);

	return isOpen;
}
void CompressBlockExec::close(void){
}
DecompressBlockExec::DecompressBlockExec() :
				cblk(nullptr),
				resno(0),
				roishift(0)
{}
bool DecompressBlockExec::open(T1Interface *t1){
	isOpen =  t1->decompress(this);

	return isOpen;
}
void DecompressBlockExec::close(void){
}

}
