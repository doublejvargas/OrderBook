#include "api/obOrderBook.hpp"

namespace ob
{
	bool OrderBook::CanMatch(Side side, Price price) const 
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

	Trades OrderBook::MatchOrders()
	{
		Trades trades{};
		trades.reserve(orders_.size());

		while (true)
		{
			if (bids_.empty() or asks_.empty())
				break;

			auto& [bidPrice, bids] = *bids_.begin(); // returns price level of buy orders with highest price, as well as a list of those buy orders
			auto& [askPrice, asks] = *asks_.begin(); // returns price level of sell orders with lowest price, as well as a list of those sell orders

			// If the highest buy offer is less than the smallest sell offer, we can't match anything (think of how the orders are organized, see comments above in "CanMatch" method)
			if (bidPrice < askPrice)
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

				if (bids.empty())
					bids_.erase(bidPrice);

				if (asks.empty())
					asks_.erase(askPrice);

				trades.push_back(Trade{
					TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
					TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity }
					});
			}
		}

		// Don't forget to handle FillAndKill orders
		if (!bids_.empty())
		{
			auto& [_, bids] = *bids_.begin();
			auto& order = bids.front();
			if (order->GetOrderType() == OrderType::FillAndKill)
				CancelOrder(order->GetOrderId());
		}

		if (!asks_.empty())
		{
			auto& [_, asks] = *asks_.begin();
			auto& order = asks.front();
			if (order->GetOrderType() == OrderType::FillAndKill)
				CancelOrder(order->GetOrderId());
		}

		return trades;
	}

	Trades OrderBook::AddOrder(OrderPointer order)
	{
		/* Exit conditions */
		if (orders_.contains(order->GetOrderId()))
			return { };

		if (order->GetOrderType() == OrderType::FillAndKill and !CanMatch(order->GetSide(), order->GetPrice()))
			return { };

		OrderPointers::iterator it{};

		if (order->GetSide() == Side::Buy)
		{
			// remember, this returns an 'OrderPointers' object, or a std::list<OrderPointer> object, to which we then add the order pointer below.
			//  notice that orders is a reference, because we need to be able to mutate the list that is contained at the price level indicated by order->GetPrice().
			auto& orders = bids_[order->GetPrice()];
			orders.push_back(order);
			// iterator is assigned to be the last element in the orders container (not 'orders.end()'), i.e, the element we just "pushed back".
			//  this corresponds to the second element of an OrderEntry object, the "location".
			it = std::next(orders.begin(), orders.size() - 1);
		}
		else
		{
			auto& orders = asks_[order->GetPrice()];
			orders.push_back(order);
			it = std::next(orders.begin(), orders.size() - 1);
		}

		orders_.insert({ order->GetOrderId(), OrderEntry{ order, it } }); // mutating internal map/state here.
		return MatchOrders();
	}

	void OrderBook::CancelOrder(OrderId orderId)
	{
		if (!orders_.contains(orderId))
			return;

		// 'order' is an OrderPointer object, 'it' is an OrderPointers::iterator object (See OrderEntry struct above, order_ and location_ fields respectively).
		const auto& [order, it] = orders_.at(orderId); //O(1) access here, since orders_ is a dictionary/map =]
		// This statement merely removes this orderId entry from the dictionary, but remember that the values stored at this key are pointers,
		//  and this pointers are not yet "destroyed" (since they are shared pointers, they are destroyed automatically once no more owners to the underlying object exist).
		orders_.erase(orderId);

		if (order->GetSide() == Side::Sell)
		{
			auto price = order->GetPrice();
			auto& orders = asks_.at(price);
			/*
			*  This is why the iterator member in an OrderEntry object is so important, because it allows to easily
			*    erase orders from the *list* of orders at any price level when calling this CancelOrder method.
			*/
			orders.erase(it);
			if (orders.empty())
				asks_.erase(price); // if no orders in this particular price level, remove it from the internal dictionary.
		}
		else
		{
			auto price = order->GetPrice();
			auto& orders = bids_.at(price);
			orders.erase(it);
			if (orders.empty())
				bids_.erase(price);
		}
	}

	/* Modify Order method
	*   can be thought of as a combination of cancel order and add order method		*/
	Trades OrderBook::MatchOrder(OrderModify order)
	{
		if (!orders_.contains(order.GetOrderId()))
			return { };

		// 'existingOrder' is an OrderPointer, '_' is a placeholder for OrderPointers::iterator object
		const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
		CancelOrder(order.GetOrderId());
		return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
	}

	OrderBookLevelInfos OrderBook::GetOrderInfos() const
	{
		LevelInfos bidInfos{}, askInfos{};
		bidInfos.reserve(orders_.size());
		askInfos.reserve(orders_.size());

		// Clever way of taking a single price level in the 'orders_' dictionary, and returning the sum of all the
		//   remaining quantities of all the orders in that price level via lambda functions, while at the same time creating a 'LevelInfo' object.
		// Take some time to understand how it works!
		auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
			{
				return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
					[](Quantity runningSum, const OrderPointer& order)
					{ return runningSum + order->GetRemainingQuantity(); }) };
			};

		for (const auto& [price, orders] : bids_)
			bidInfos.push_back(CreateLevelInfos(price, orders));

		for (const auto& [price, orders] : asks_)
			askInfos.push_back(CreateLevelInfos(price, orders));

		return OrderBookLevelInfos{ bidInfos, askInfos };
	}
}