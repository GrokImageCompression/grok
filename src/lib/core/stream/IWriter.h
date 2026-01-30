#pragma once

#include <cstdint>

namespace grk
{

struct IWriter
{
  virtual ~IWriter() = default;

  /**
   * @brief Writes to stream
   *
   * @tparam TYPE type of value to write
   * @param value value
   * @return true if successful
   */
  template<typename TYPE>
  bool write(TYPE value)
  {
    return write_non_template((const uint8_t*)&value, sizeof(TYPE), sizeof(TYPE));
  }

protected:
  virtual bool write_non_template(const uint8_t* value, uint8_t sizeOfType, uint8_t numBytes) = 0;
};

} // namespace grk