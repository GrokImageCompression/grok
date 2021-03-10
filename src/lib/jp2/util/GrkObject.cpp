#include "grk_includes.h"

namespace grk {

GrkObject::GrkObject() : ref_count(1){
}

GrkObject::~GrkObject(){

}

uint32_t GrkObject::refcount(void){
	return ref_count;
}

GrkObject* GrkObject::ref(void){
	ref_count++;

	return this;
}
GrkObject* GrkObject::unref(void){
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
