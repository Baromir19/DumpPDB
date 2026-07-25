#pragma once 

#include <Util\Types\Serialization\IBaseSerializableBase.hpp>

#include <Application\String\StringManager.hpp>

#define SERIALIZABLE(typename, parent_name, var_name, basevalue) \
	typename ## Serializable var_name = typename ## Serializable(basevalue, HashManager::crc32(#parent_name "::" #var_name), #parent_name "::" #var_name);

template<typename T>
class IBaseSerializable : public IBaseSerializableBase
{
protected:
	// T mvalue;
	// const size_t m_size = sizeof(T);
	// const std::wstring m_name;
	SaveManager::Entry<T> m_entry;
	const char* m_name;

public:
	IBaseSerializable(
		T a_baseValue,
		SaveManager::TypeId a_typeId, 
		const unsigned __int32 a_hash,
		const char* name = ""
	)
		:	m_entry{ a_typeId, a_hash, a_baseValue },
			m_name(name)
	{
		load();
		m_instances.push_back(this);
	};

	IBaseSerializable(T a_baseValue, SaveManager::TypeId a_typeId, const char* a_name = "")
		: IBaseSerializable(a_baseValue, a_typeId, HashManager::crc32(a_name), a_name) { };

	T getValue() const { return m_entry.mvalue; }
	// void setValue(T a_newValue) { m_entry.mvalue = a_newValue; } // specially for mngr
	void setValue(int a_newValue) override { m_entry.mvalue = a_newValue; }

	unsigned __int32 getHash() const override { return m_entry.m_hash; }

	void displayInfo() const override 
	{ 
		ConsoleManager::print(L"    %-45s (value: %u, type: 0x%X)\n",
			StringManager::convertCharToWChar(m_name).c_str(),
			m_entry.mvalue,
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

	operator T() const { return m_entry.mvalue; }

	~IBaseSerializable()
	{
		m_instances.remove(this);
	}
};