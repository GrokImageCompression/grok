#pragma once

namespace grk {

#include "grok.h"

class GrkObject {
public:
	explicit GrkObject(void);
	virtual ~GrkObject(void);
	GrkObject* ref(void);
	GrkObject* unref(void);
	uint32_t refcount(void);
	virtual void release(void) = 0;
private:
	uint32_t ref_count;
};


template<typename T> class GrkObjectImpl : public GrkObject {
public:
	explicit GrkObjectImpl(T *wrap) : wrappee(wrap)
	{}
	virtual ~GrkObjectImpl(void){}
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
