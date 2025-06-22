#pragma once

#include "..\..\..\Application\Hash\HashManager.hpp"
#include "..\..\..\Application\Save\SaveManager.hpp"
#include "..\..\..\Application\String\StringManager.hpp"

#include <list>

class IBaseSerializableBase
{
protected:
	static inline std::list<IBaseSerializableBase*> m_instances;

public:
	// template<typename T>
	virtual void setValue(int a_newValue) = 0; // specially for mngr

	virtual unsigned __int32 getHash() const = 0;

	virtual bool save() const = 0;

	virtual bool load() = 0;

	virtual void displayInfo() const = 0;

	static void displayInstancesInfo()
	{
		for (auto _instance : m_instances) { _instance->displayInfo(); }
	}

	static void setInstance(const char* a_name, int a_value)
	{
		// DebugManager::WaitDebugger();

		auto _hash = HashManager::crc32(a_name);

		for (auto _instance : m_instances)
		{
			if (_instance->getHash() == _hash)
			{
				_instance->setValue(a_value);
				_instance->save();
				ConsoleManager::print(L"Written value %i to setting %s \n", a_value, StringManager::convertCharToWChar(a_name).c_str());
				return;
			}
		}

		ConsoleManager::printError(L"The setting was not found! \n");
	}
};