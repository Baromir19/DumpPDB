#pragma once 

#include "IBaseSerializable.hpp"

class BoolSerializable : public IBaseSerializable<bool>
{
protected:

public:
	BoolSerializable(bool _baseValue = false, const wchar_t* _name = L"") :
		IBaseSerializable(_baseValue, TYPE_BOOL, _name) {};
};