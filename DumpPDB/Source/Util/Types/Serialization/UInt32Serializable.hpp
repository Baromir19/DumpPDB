#pragma once 

#include "IBaseSerializable.hpp"

class UInt32Serializable : public IBaseSerializable<unsigned __int32>
{
protected:

public:
	UInt32Serializable(unsigned __int32 _baseValue = 0, const char* _name = "") :
		IBaseSerializable(_baseValue, SaveManager::TYPE_UINT32, _name) {};

	UInt32Serializable(unsigned __int32 _baseValue, unsigned __int32 _hash, const char* _name = "") :
		IBaseSerializable(_baseValue, SaveManager::TYPE_UINT32, _hash, _name) {};
};