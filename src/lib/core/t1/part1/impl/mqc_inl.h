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

#define BYPASS_CT_INIT 0xDEADBEEF

#define PUSH_MQC()                         \
   const mqc_state** curctx = mqc->curctx; \
   uint32_t c = mqc->c;                    \
   uint32_t a = mqc->a;                    \
   uint32_t ct = mqc->ct

#define POP_MQC()        \
   mqc->curctx = curctx; \
   mqc->c = c;           \
   mqc->a = a;           \
   mqc->ct = ct;

#ifdef PLUGIN_DEBUG_ENCODE
#define mqc_setcurctx(mqc, ctxno)           \
   (mqc)->debug_mqc.context_number = ctxno; \
   (mqc)->curctx = (mqc)->ctxs + (uint32_t)(ctxno)
#else

#define mqc_setcurctx(mqc, ctxno) (mqc)->curctx = (mqc)->ctxs + (uint32_t)(ctxno)

#endif
