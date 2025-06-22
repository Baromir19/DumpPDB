#pragma once

#include <list>

template<typename T>
class IBaseSerializable;

static std::list<IBaseSerializable<unsigned int>*> m_instances;

#include "IBaseSerializable.hpp"