#include "grk_includes.h"

namespace grk
{
GrkObjectWrapper::GrkObjectWrapper() : ref_count(1) {}

GrkObjectWrapper::~GrkObjectWrapper() {}
int32_t GrkObjectWrapper::ref(void)
{
   return ref_count++;
}
int32_t GrkObjectWrapper::unref(void)
{
   if(ref_count == 0)
	  Logger::logger_.warn("Attempt to uref a release object");
   else if(--ref_count == 0)
	  release();

   return ref_count;
}

} // namespace grk
