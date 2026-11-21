/*
 * mercury streaming full-image fast path.
 *
 * grk_decompress_init records the input file path; inside
 * CodeStreamDecompress::decompress, mercuryFastPath() plans the stream
 * through mercury's C API (include/mercury_capi.h in the mercury repo)
 * and, if accepted, decodes it with grok's own part-1 block coder (the
 * mercury_grok_t1_decode shim) straight into multiTileComposite_'s
 * planes. Anything the plan rejects falls back to the classic pipeline.
 *
 * On by default. GRK_MERCURY=0 in the environment forces the classic pipeline.
 */
#pragma once

namespace grk
{
class CodeStreamDecompress;

/* Try the mercury fast path. Returns true if the composite image was
 * fully decoded and filled; false = caller proceeds with the classic
 * pipeline (never partially modifies the image on failure). */
bool mercuryFastPath(CodeStreamDecompress& cs);

} // namespace grk
