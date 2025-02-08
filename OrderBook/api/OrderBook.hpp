#pragma once

#include "api/obAliases.hpp"
#include "api/obSide.hpp"
#include "api/obOrder.hpp"
#include "api/obTrade.hpp"

//lib
#include <map>
#include <unordered_map>

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

		bool CanMatch(Side side, Price price) const
		{
			if (side == Side::Buy)
			{
				if (asks_.empty())
					return false;

				// 'asks_' is a map that contains keys of type 'Price' and values of type 'OrderPointers', which is a list of 
				//   OrderPointer objects (or shared pointers that point to 'Order') [complex, I know!]
				//  'asks_.begin()' returns an iterator to the first element in this map, which is an iterator/pointer to the first element.
				//  Deferencing this iterator returns a pair of type 'pair<Price, OrderPointers>' [simplified]
				//  I then assign this pair to two variables, 'bestAsk' (the key) and '_' (the list of orders at the price level).
				//  Because 'asks_' is sorted in ascending order, the first element will be the best (or lowest) selling price.
				const auto& [bestAsk, _] = *asks_.begin();
				return price >= bestAsk;
			}
			else
			{
				if (bids_.empty())
					return false;

				const auto& [bestBid, _] = *bids_.begin();
				return price <= bestBid;
			}
		}

		Trades MatchOrders()
		{
			Trades trades;
			trades.reserve(orders_.size());

			while (true)
			{
				if (bids_.empty() or asks_.empty())
					break;

				auto& [bidPrice, bids] = *bids_.begin(); // returns price level of buy orders with highest price, as well as a list of those buy orders
				auto& [askPrice, asks] = *asks_.begin(); // returns price level of sell orders with lowest price, as well as a list of those sell orders

				if (bidPrice < askPrice) // if the highest buy offer is less than the smallest sell offer, we can't match anything (think of how the orders are organized, see comments above)
					break;

				while (bids.size() and asks.size())
				{
					auto& bid = bids.front();
					auto& ask = asks.front();

					// The quantity that can be filled is the minimum between both orders, as we cannot "overfill" an order.
					Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

					bid->Fill(quantity);
					ask->Fill(quantity);

					if (bid->IsFilled())
					{
						bids.pop_front();
						orders_.erase(bid->GetOrderId());
					}

					if (ask->IsFilled())
					{
						asks.pop_front();
						orders_.erase(ask->GetOrderId());
					}

					if (bids_.empty())
						bids_.erase(bidPrice);

					if (asks_.empty())
						asks_.erase(askPrice);

					trades.push_back(Trade{ 
						TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity}, 
						TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity} 
						});
				}
			}
		}
	};
}