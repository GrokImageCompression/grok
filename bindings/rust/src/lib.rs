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
 */

pub mod errors;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_UNK as GRK_FMT_UNKNOWN;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_J2K as GRK_FMT_J2K;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_JP2 as GRK_FMT_JP2;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_PXM as GRK_FMT_PXM;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_PGX as GRK_FMT_PGX;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_PAM as GRK_FMT_PAM;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_BMP as GRK_FMT_BMP;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_TIF as GRK_FMT_TIF;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_RAW as GRK_FMT_RAW;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_PNG as GRK_FMT_PNG;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_RAWL as GRK_FMT_RAWL;
pub use _GRK_SUPPORTED_FILE_FMT_GRK_FMT_JPG as GRK_FMT_JPG;


