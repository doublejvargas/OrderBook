#pragma once

#include "api/obAliases.hpp"

namespace ob
{
	struct TradeInfo
	{
		OrderId orderId_;
		Price price_;
		Quantity quantity_;
	};

	class Trade
	{
	public:

		Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
			: bidTrade_{ bidTrade }
			, askTrade_{ askTrade }
		{ }

		const TradeInfo& GetBidTrade() const { return bidTrade_; }
		const TradeInfo& GetAskTrade() const { return askTrade_; }

	private:

		TradeInfo bidTrade_;
		TradeInfo askTrade_;
	};
}