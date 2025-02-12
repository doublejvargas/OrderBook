#pragma once

#include "api/obAliases.hpp"
#include "api/obSide.hpp"
#include "api/obOrder.hpp"
#include "api/obTrade.hpp"
#include "api/obOrderModify.hpp"
#include "api/obOrderBookLevelInfos.hpp"

//lib
#include <map>
#include <unordered_map>
#include <numeric>

namespace ob
{
	class OrderBook
	{
	private:

		struct OrderEntry
		{
			OrderPointer order_{ nullptr };
			OrderPointers::iterator location_;
		};

		/* These ordered maps organize orders by Price-Time priority.
		*  This means that orders are first organized by price in the map, as price is the key of the map,
		*  and then, orders in the same price level are organized by time priority, since the data structure in the value is a list,
		*  meaning that if we retrieve the first item in this list, it corresponds to the first order that was placed for this particular price level.
		*/
		std::map<Price, OrderPointers, std::greater<Price>> bids_;
		std::map<Price, OrderPointers, std::less<Price>> asks_;
		std::unordered_map<OrderId, OrderEntry> orders_;

		bool CanMatch(Side side, Price price) const;

		Trades MatchOrders();

	public:

		Trades AddOrder(OrderPointer order);
		void CancelOrder(OrderId orderId);
		/* Modify Order method
		*   can be thought of as a combination of cancel order and add order methods */
		Trades MatchOrder(OrderModify order);
		std::size_t Size() const { return orders_.size(); }
		
		OrderBookLevelInfos GetOrderInfos() const;
	};
}