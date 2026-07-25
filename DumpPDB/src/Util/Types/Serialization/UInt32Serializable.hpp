#pragma once 

#include <Util\Types\Serialization\IBaseSerializable.hpp>

class UInt32Serializable : public IBaseSerializable<unsigned __int32>
{
protected:

public:
	UInt32Serializable(unsigned __int32 a_baseValue = 0, const char* a_name = "") :
		IBaseSerializable(a_baseValue, SaveManager::TYPE_UINT32, a_name) {};

	UInt32Serializable(unsigned __int32 a_baseValue, unsigned __int32 a_hash, const char* a_name = "") :
		IBaseSerializable(a_baseValue, SaveManager::TYPE_UINT32, a_hash, a_name) {};
};