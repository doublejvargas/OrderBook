#pragma once

//lib
#include <limits>

#include "api/obAliases.hpp"

namespace ob
{
	struct Constants
	{
		static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
	};
}