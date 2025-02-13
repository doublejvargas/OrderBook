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
#include <mutex>

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

		struct LevelData
		{
			Quantity quantity_{};
			Quantity count_{};

			enum class Action
			{
				Add,
				Remove,
				Match
			};
		};

		// Bookkeeping data structure
		std::unordered_map<Price, LevelData> data_;
		/* These ordered maps organize orders by Price-Time priority.
		*  This means that orders are first organized by price in the map, as price is the key of the map,
		*  and then, orders in the same price level are organized by time priority, since the data structure in the value is a list,
		*  meaning that if we retrieve the first item in this list, it corresponds to the first order that was placed for this particular price level.
		*/
		std::map<Price, OrderPointers, std::greater<Price>> bids_{};
		std::map<Price, OrderPointers, std::less<Price>> asks_{};
		std::unordered_map<OrderId, OrderEntry> orders_{};
		mutable std::mutex ordersMutex_{};
		/* The purpose of this thread is to wait until the end of the trading day, and then submit an unsolicited cancel to all GoodTillDay orders */
		std::thread ordersPruneThread_{};
		std::condition_variable shutdownConditionVariable_{};
		std::atomic<bool> shutdown_{ false };

		void PruneGoodForDayOrders();

		bool CanFullyFill(Side side, Price price, Quantity quantity) const;
		bool CanMatch(Side side, Price price) const;
		Trades MatchOrders();

		void OnOrderCancelled(OrderPointer order);
		void OnOrderAdded(OrderPointer order);
		void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
		void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

		void CancelOrders(OrderIds orderIds);
		void CancelOrderInternal(OrderId orderId);

	public:

		// Constructor and destructor
		OrderBook();
		~OrderBook();

		Trades AddOrder(OrderPointer order);
		void CancelOrder(OrderId orderId);
		/* Modify Order method
		*   can be thought of as a combination of cancel order and add order methods */
		Trades MatchOrder(OrderModify order);
		std::size_t Size() const { return orders_.size(); }

		OrderBookLevelInfos GetOrderInfos() const;
	};
};