#pragma once

#include <cvconst.h>

class GlobalSettings
{
public:
	static inline bool s_isSizeInfo = true; // info about struct size
	static inline bool s_isOffsetInfo = true; // info about struct fields offsets
	static inline bool s_isCurlyBracketTransfer = true; // is C-style for "{" ?
	static inline bool s_isEnumInheritence = true; // is enum base type should be written
	static inline bool s_isCaseSensitiveSearch = false;
	static inline bool s_isShowAccess = true;
	static inline bool s_isNonScoped = true;
	static inline bool s_isEnumHex = false;

	static inline CV_access_e s_baseAccessType = (CV_access_e)false;
};