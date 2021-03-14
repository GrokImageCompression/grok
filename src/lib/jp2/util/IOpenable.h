#pragma once

namespace grk {

class T1Interface;

class IOpenable{
public:
	virtual ~IOpenable() = default;
	virtual bool open(void) = 0;
	virtual void close(void) = 0;
};

}
