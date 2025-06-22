#pragma once 

#include "IBaseSerializableBase.hpp"

#include "..\..\..\Application\String\StringManager.hpp"

#define SERIALIZABLE(type_name, parent_name, var_name, base_value) \
	type_name ## Serializable var_name = type_name ## Serializable(base_value, HashManager::crc32(#parent_name "::" #var_name), #parent_name "::" #var_name);

template<typename T>
class IBaseSerializable : public IBaseSerializableBase
{
protected:
	// T m_value;
	// const size_t m_size = sizeof(T);
	// const std::wstring m_name;
	SaveManager::Entry<T> m_entry;
	const char* m_name;

public:
	IBaseSerializable(T _baseValue, SaveManager::TypeId _typeId, const unsigned __int32 m_hash, const char* _name = "")
		:	m_entry{ _typeId, m_hash, _baseValue },
			m_name(_name)
	{
		load();
		m_instances.push_back(this);
	};

	IBaseSerializable(T _baseValue, SaveManager::TypeId _typeId, const char* _name = "")
		: IBaseSerializable(_baseValue, _typeId, HashManager::crc32(_name), _name) { };

	T getValue() const { return m_entry.m_value; }
	// void setValue(T a_newValue) { m_entry.m_value = a_newValue; } // specially for mngr
	void setValue(int a_newValue) override { m_entry.m_value = a_newValue; }

	unsigned __int32 getHash() const override { return m_entry.m_hash; }

	void displayInfo() const override 
	{ 
		ConsoleManager::print(L"\t%-45s (value: %u, type: 0x%X)\n",
			StringManager::convertCharToWChar(m_name).c_str(),
			m_entry.m_value,
			m_entry.m_typeId);
	}

	bool save() const override
	{
		// DebugManager::WaitDebugger();
		SaveManager::instance().setEntry(m_entry);
		return true;
	}

	bool load() override
	{
		SaveManager::instance().getEntry(m_entry);
		return true;
	}

	operator T() const { return m_entry.m_value; }

	~IBaseSerializable()
	{
		m_instances.remove(this);
	}
};