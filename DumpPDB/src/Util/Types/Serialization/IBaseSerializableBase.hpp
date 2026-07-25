#pragma once

#include <Application\Hash\HashManager.hpp>
#include <Application\String\StringManager.hpp>
#include <Application\Save\SaveManager.hpp>

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
		for (auto instance : m_instances) { instance->displayInfo(); }
	}

	static void setInstance(const char* a_name, int avalue)
	{
		// DebugManager::WaitDebugger();

		auto hash = HashManager::crc32(a_name);

		for (auto instance : m_instances)
		{
			if (instance->getHash() == hash)
			{
				instance->setValue(avalue);
				instance->save();
				ConsoleManager::print(L"Written value %i to setting %s \n", avalue, StringManager::convertCharToWChar(a_name).c_str());
				return;
			}
		}

		ConsoleManager::printError(L"The setting was not found! \n");
	}
};