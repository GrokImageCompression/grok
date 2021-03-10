#include "grk_includes.h"

namespace grk {

GrkObjectWrapper::GrkObjectWrapper() : ref_count(1){
}

GrkObjectWrapper::~GrkObjectWrapper(){

}

uint32_t GrkObjectWrapper::refcount(void){
	return ref_count;
}

GrkObjectWrapper* GrkObjectWrapper::ref(void){
	ref_count++;

	return this;
}
GrkObjectWrapper* GrkObjectWrapper::unref(void){
	if (ref_count == 0){
		GRK_WARN("Attempt to uref an object with ref count 0");
	} else {
		ref_count--;
		if (ref_count == 0)
			release();
	}

	return this;
}

}
