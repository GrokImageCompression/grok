#pragma once

#include <cstdint>

namespace grk
{

struct IStreamWriter
{
  virtual ~IStreamWriter() = default;

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

  /**
   * @brief Writes byte
   *
   * Endian is NOT taken into account
   * @param value byte to write
   * @return true if successful
   */
  virtual bool write8u(uint8_t value) = 0;

protected:
  virtual bool write_non_template(const uint8_t* value, uint8_t sizeOfType, uint8_t numBytes) = 0;
};

} // namespace grk