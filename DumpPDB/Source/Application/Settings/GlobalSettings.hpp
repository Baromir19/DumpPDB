#pragma once

#include <cvconst.h>

#include "..\..\Util\Types\Serialization\BoolSerializable.hpp"
#include "..\..\Util\Types\Serialization\UInt32Serializable.hpp"

class GlobalSettings
{
public:
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isSizeInfo, true)// info about struct size
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isOffsetInfo, true) // info about struct fields offsets
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isCurlyBracketTransfer, true) // is C-style for "{" ?
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isEnumInheritence, true)  // is enum base type should be written
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isCaseSensitiveSearch, false)
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isShowAccess, true)
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isNonScoped, true)
	static inline SERIALIZABLE(Bool, GlobalSettings, s_isEnumHex, false)
	static inline SERIALIZABLE(Bool, GlobalSettings, s_typeSource, true)
	static inline SERIALIZABLE(Bool, GlobalSettings, s_infoComment, true)
	static inline SERIALIZABLE(UInt32, GlobalSettings, s_baseAccessType, 0) // CV_access_e
};