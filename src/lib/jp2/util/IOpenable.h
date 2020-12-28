#pragma once

namespace grk {

class T1Interface;

class IOpenable{
public:
	virtual ~IOpenable(){}
	virtual bool open(T1Interface *t1) = 0;
	virtual void close(void) = 0;
};

}
