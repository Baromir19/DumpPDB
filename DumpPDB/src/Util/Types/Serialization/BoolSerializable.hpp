#pragma once 

#include <Util\Types\Serialization\IBaseSerializable.hpp>

class BoolSerializable : public IBaseSerializable<bool>
{
protected:

public:
	BoolSerializable(bool a_baseValue = false, const char* a_name = "") :
		IBaseSerializable(a_baseValue, SaveManager::TYPE_BOOL, a_name) {};

	BoolSerializable(bool a_baseValue, unsigned __int32 a_hash, const char* a_name = "") :
		IBaseSerializable(a_baseValue, SaveManager::TYPE_BOOL, a_hash, a_name) {};
};