#pragma once

#include "api/obLevelInfo.hpp"

namespace ob
{
	class OrderBookLevelInfos
	{
	public:
		OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
			: bids_ { bids }
			, asks_ { asks }
		{ }

		// Getters
		//  Notice that these getters return const objects that cannot be modified.
		const LevelInfos& GetBids() const { return bids_; }
		const LevelInfos& GetAsks() const { return asks_; }

	private:
		LevelInfos bids_;
		LevelInfos asks_;
	};
}