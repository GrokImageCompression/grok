#pragma once

namespace grk {

#include "grok.h"

class GrkObjectWrapper {
public:
	explicit GrkObjectWrapper(void);
	virtual ~GrkObjectWrapper(void);
	GrkObjectWrapper* ref(void);
	GrkObjectWrapper* unref(void);
	uint32_t refcount(void);
	virtual void release(void) = 0;
private:
	uint32_t ref_count;
};


template<typename T> class GrkObjectWrapperImpl : public GrkObjectWrapper {
public:
	explicit GrkObjectWrapperImpl(T *wrap) : wrappee(wrap)
	{}
	virtual ~GrkObjectWrapperImpl(void) = default;
	virtual void release(void){
		assert(wrappee);
		delete wrappee;
		wrappee = nullptr;
	}
	T* getWrappee(void){
		assert(wrappee);
		return wrappee;
	}
private:
	T *wrappee;
};


}
