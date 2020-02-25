#pragma once

#include <cstdint>


namespace grk {


struct grk_j2k;

/**
 * Quantization stepsize
 */
struct grk_stepsize {
	/** exponent - 5 bits */
	uint8_t expn;
	/** mantissa  -11 bits */
	uint16_t mant;
};



struct grk_j2k;
struct grk_tccp;
struct BufferedStream;
struct grk_tcd_band;
struct grk_tcp;


class Quantizer {
public:

	void setBandStepSizeAndBps( grk_tcp *tcp,
								grk_tcd_band *band,
								uint32_t resno,
								 uint8_t bandno,
								grk_tccp *tccp,
								uint32_t image_precision,
								float fraction);


	/**
	 Explicit calculation of the Quantization Stepsizes
	 @param tccp Tile-component coding parameters
	 @param prec Precint analyzed
	 */
	static void calc_explicit_stepsizes(grk_tccp *tccp, uint32_t prec);

	static void encode_stepsize(int32_t stepsize, int32_t numbps,
			grk_stepsize *bandno_stepsize);

	uint32_t get_SQcd_SQcc_size(grk_j2k *p_j2k, uint16_t tile_no,
			uint32_t comp_no);
	bool compare_SQcd_SQcc(grk_j2k *p_j2k, uint16_t tile_no,
			uint32_t first_comp_no, uint32_t second_comp_no);
	bool read_SQcd_SQcc(bool fromQCC, grk_j2k *p_j2k, uint32_t comp_no,
			uint8_t *p_header_data, uint16_t *header_size);
	bool write_SQcd_SQcc(grk_j2k *p_j2k, uint16_t tile_no,
			uint32_t comp_no, BufferedStream *p_stream);
	void apply_quant(grk_tccp *src, grk_tccp *dest);
};

}
