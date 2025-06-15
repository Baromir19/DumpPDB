#pragma once 

#include <string>

#include <winnt.h>
#include <fileapi.h>
#include <minwindef.h>

template<typename T>
class IBaseSerializable
{
protected:
	enum TypeId : unsigned __int16
	{
		TYPE_INT32 = 0,
		TYPE_UINT32,
		TYPE_INT16,
		TYPE_UINT16,
		TYPE_INT8,
		TYPE_UINT8,
		TYPE_BOOL,
		TYPE_FLOAT,
	};

	T m_value;
	const size_t m_size = sizeof(T);
	const std::wstring m_name;
	const TypeId m_typeId;

public:
	IBaseSerializable(T _baseValue, const TypeId _typeId, const wchar_t* _name = L"")
		: m_value(_baseValue), m_typeId(_typeId), m_name(_name) {};

	T getValue() const { return m_value; }
	void setValue(T a_newValue) { m_value = a_newValue; } // specially for mngr

	bool save(HANDLE a_hFile) const
	{
		struct __mainDataWrite
		{
			TypeId m_typeId;
			T m_value;
			unsigned __int16 m_nameSize;
		} _buffer;

		_buffer.m_typeId = this->m_typeId;
		_buffer.m_value = this->m_value;
		_buffer.m_nameSize = this->m_name.size();

		DWORD _bytesWritten;
		WriteFile(a_hFile, &_buffer, sizeof(_buffer), &_bytesWritten, NULL);

		if (_bytesWritten != sizeof(_buffer))
		{
			return false;
		}

		WriteFile(a_hFile, m_name.c_str(), m_name.size(), &_bytesWritten, NULL);

		return m_name.size() != sizeof(_buffer) ? false : true;
	}

	bool load(HANDLE a_hFile) 
	{
		// MYSTRUCT buffer;
		TypeId _typeId;
		DWORD _bytesRead;
		ReadFile(a_hFile, &_typeId, sizeof(_typeId), &_bytesRead, NULL); // get file size + checks
	}
};