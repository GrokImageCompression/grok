#pragma once

namespace grk {

grk_stream* create_mapped_file_read_stream(const char *fname);
grk_stream* create_mapped_file_write_stream(const char *fname);

}
