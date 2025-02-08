#pragma once

//lib
#include <vector>

#include "api/obAliases.hpp"

namespace ob
{
	struct LevelInfo
	{
		Price price_;
		Quantity quantity_;
	};

	//using LevelInfos = std::vector<LevelInfo>;
}