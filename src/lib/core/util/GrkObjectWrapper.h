#pragma once

#include "grok.h"
#include <atomic>

namespace grk
{
class GrkObjectWrapper
{
 public:
   explicit GrkObjectWrapper(void);
   virtual ~GrkObjectWrapper(void);
   int32_t ref(void);
   int32_t unref(void);
   virtual void release(void) = 0;

 private:
   std::atomic<int32_t> ref_count;
};

template<typename T>
class GrkObjectWrapperImpl : public GrkObjectWrapper
{
 public:
   explicit GrkObjectWrapperImpl(T* wrap) : wrappee(wrap) {}
   virtual ~GrkObjectWrapperImpl(void) = default;
   virtual void release(void)
   {
	  assert(wrappee);
	  delete wrappee;
	  wrappee = nullptr;
   }
   T* getWrappee(void)
   {
	  assert(wrappee);
	  return wrappee;
   }

 private:
   T* wrappee;
};

} // namespace grk
