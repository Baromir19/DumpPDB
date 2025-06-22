#pragma once

#include <winerror.h>
#include <cstdio>
#include <windows.h>
#include <ShlObj.h>

#include "..\..\Util\Container\Singleton.hpp"
#include "..\Console\ConsoleManager.hpp"
#include "..\Debug\DebugManager.hpp"

class SaveManager : public Singleton<SaveManager>
{
	SET_SINGLETON_FRIEND(SaveManager)

public:
	static constexpr int s_bufferSize = 0x1000;

protected:
	unsigned __int8 alignas(4) m_saveBuffer[s_bufferSize];
	unsigned int m_cursor = 0;

	static inline const wchar_t* s_filename = L"save.dat";
	static inline const wchar_t* s_folderName = L"DumpPDB";
	static constexpr unsigned int s_headerValue = 'Dpdb';

	std::wstring m_fullPath;

	class HeaderFormat
	{
	public:
		unsigned int m_header;
		unsigned int m_entries;
	};

	unsigned int& m_entriesCount = (*(HeaderFormat*)m_saveBuffer).m_entries;
	FILE* m_file = nullptr;

	static constexpr unsigned int s_entriesBegin = sizeof(HeaderFormat);

public:
	enum TypeId : unsigned __int32
	{
		TYPE_INT8,
		TYPE_UINT8,
		TYPE_BOOL,

		TYPE_INT16,
		TYPE_UINT16,

		TYPE_FLOAT,
		TYPE_INT32,
		TYPE_UINT32,
	};

	template<typename T>
	struct alignas(4) Entry
	{
		TypeId m_typeId;
		unsigned __int32 m_hash;
		T m_value;
	};

	static int __fastcall getEntrySize(TypeId a_type)
	{
		switch (a_type)
		{
		case TYPE_UINT8:
		case TYPE_INT8:
		case TYPE_BOOL:
			return sizeof(Entry<__int8>);

		case TYPE_INT16:
		case TYPE_UINT16:
			return sizeof(Entry<__int16>);

		case TYPE_INT32:
		case TYPE_UINT32:
		case TYPE_FLOAT:
			return sizeof(Entry<__int32>);

		default: return 0;
		};
	}

	static int __fastcall getTypeSize(TypeId a_type) 
	{
		switch (a_type)
		{
		case TYPE_UINT8:
		case TYPE_INT8:
		case TYPE_BOOL: 
			return sizeof(__int8);

		case TYPE_INT16:
		case TYPE_UINT16:
			return sizeof(__int16);

		case TYPE_INT32:
		case TYPE_UINT32:
		case TYPE_FLOAT:
			return sizeof(__int32);

		default: return 0;
		};
	}

	unsigned __int8* getBufferBegin() { return m_saveBuffer; }

	unsigned int getEntriesCount() const { return (*(HeaderFormat*)m_saveBuffer).m_entries; }

	bool isHeaderValid() const { return (*(HeaderFormat*)m_saveBuffer).m_header == s_headerValue; }

	void initHeader(int _entries = 0) 
	{ 
		(*(HeaderFormat*)m_saveBuffer).m_header = s_headerValue;
		(*(HeaderFormat*)m_saveBuffer).m_entries = _entries;
	}

	template<typename T>
	bool appendEntry(Entry<T> a_entry) 
	{ 
		if (m_cursor + sizeof(Entry<T>) > s_bufferSize) { exit(ERROR_HANDLE_EOF); }
		(*(Entry<T>*)(&m_saveBuffer[m_cursor])) = a_entry; // ???
		m_cursor += sizeof(Entry<T>);
		++m_entriesCount;
		return true;
	}

	template<typename T>
	void __fastcall getEntry(Entry<T>& a_data)
	{
		// DebugManager::WaitDebugger();

		m_cursor = s_entriesBegin;

		int _entryIter = m_entriesCount;

		while (_entryIter-- > 0 && m_cursor + sizeof(Entry<T>) <= s_bufferSize)
		{
			auto _entry = *(Entry<T>*)(&m_saveBuffer[m_cursor]);

			if (_entry.m_typeId == a_data.m_typeId 
				&& _entry.m_hash == a_data.m_hash)
			{
				a_data.m_value =  _entry.m_value;
				return;
			}

			m_cursor += getEntrySize(_entry.m_typeId);
		}

		appendEntry(a_data);
	}

	template<typename T>
	void __fastcall setEntry(const Entry<T>& a_data)
	{
		// DebugManager::WaitDebugger();

		m_cursor = s_entriesBegin;

		int _entryIter = m_entriesCount;

		while (_entryIter-- > 0 && m_cursor + sizeof(Entry<T>) <= s_bufferSize)
		{
			auto& _entry = *(Entry<T>*)(&m_saveBuffer[m_cursor]);

			if (_entry.m_typeId == a_data.m_typeId
				&& _entry.m_hash == a_data.m_hash)
			{
				_entry.m_value = a_data.m_value;
				return;
			}

			m_cursor += getEntrySize(_entry.m_typeId);
		}

		appendEntry(a_data);
	}

protected:
	std::wstring getDocumentsPath()
	{
		wchar_t _path[MAX_PATH] = { 0 };

		if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, _path)))
		{
			return std::wstring(_path);
		}
		else
		{
			return L"";
		}
	}

	void save()
	{
		errno_t _err;

		if (!m_file) { _err = _wfopen_s(&m_file, m_fullPath.c_str(), L"wb"); }

		if (!m_file) { ConsoleManager::printError(L"File creation error: %i\n", _err); }

		size_t _written = fwrite(m_saveBuffer, 1, sizeof(m_saveBuffer), m_file);

		if (_written != sizeof(m_saveBuffer)) { ConsoleManager::printError(L"File size error: %i\n", _written); }

		if (m_file) { fclose(m_file); }
		m_file = nullptr;
	}

	void load()
	{
		errno_t _err;

		if (!m_file) { _err = _wfopen_s(&m_file, m_fullPath.c_str(), L"rb+"); }

		if (!m_file) { ConsoleManager::printError(L"File opening error: %i\n", _err); }

		if (!(fread(m_saveBuffer, 1, sizeof(m_saveBuffer), m_file) == sizeof(m_saveBuffer)))
		{
			exit(ERROR);
		}

		if (m_file) { fclose(m_file); }

		m_file = nullptr;

		m_cursor = s_entriesBegin;
	}

protected:
	SaveManager()
	{
		wchar_t _buffer[MAX_PATH];

		m_fullPath = getDocumentsPath();

		if (m_fullPath.empty())
		{
			DWORD _length = GetCurrentDirectoryW(MAX_PATH, _buffer);
			if (_length <= 0 || _length >= MAX_PATH) { exit(ERROR); }
			m_fullPath = _buffer;
		}
		else
		{
			m_fullPath += L"\\";
			m_fullPath += s_folderName;

			if (!CreateDirectoryW(m_fullPath.c_str(), nullptr))
			{
				DWORD _err = GetLastError();
				if (_err != ERROR_ALREADY_EXISTS)
				{
					ConsoleManager::printError(L"Folder creation error: %i\n", _err);
				}
			}
		}

		m_fullPath += L"\\";
		m_fullPath += s_filename;

		DWORD _attr = GetFileAttributesW(m_fullPath.c_str());

		// ConsoleManager::print(m_fullPath.c_str());

		if (_attr == INVALID_FILE_ATTRIBUTES || (_attr & FILE_ATTRIBUTE_DIRECTORY))
		{
			initHeader();
			save();
		}

		// SetFileAttributesW(m_fullPath.c_str(), FILE_ATTRIBUTE_HIDDEN); // FILE_ATTRIBUTE_SYSTEM

		load();

		return;
	}

	~SaveManager()
	{
		save();

		if (m_file) { fclose(m_file); }
	}
};