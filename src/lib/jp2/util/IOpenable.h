#pragma once

class IOpenable{
public:
	virtual ~IOpenable(){}
	virtual bool open(void) = 0;
	virtual void close(void) = 0;
};
