#pragma once 

#include "IBaseSerializable.hpp"

class BoolSerializable : public IBaseSerializable<bool>
{
protected:

public:
	BoolSerializable(bool _baseValue = false, const char* _name = "") :
		IBaseSerializable(_baseValue, SaveManager::TYPE_BOOL, _name) {};

	BoolSerializable(bool _baseValue, unsigned __int32 _hash, const char* _name = "") :
		IBaseSerializable(_baseValue, SaveManager::TYPE_BOOL, _hash, _name) {};
};