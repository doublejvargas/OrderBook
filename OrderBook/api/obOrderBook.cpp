#include "api/obOrderBook.hpp"

// lib
#include <numeric>
#include <chrono>


namespace ob
{
	/*******************************************************************
	*							Private API							   *
	********************************************************************/
	void OrderBook::PruneGoodForDayOrders()
	{
		using namespace std::chrono;
		const auto end = hours(16); // 4pm (military time)

		// Thread is looping
		while (true)
		{
			// time_point value
			const auto now = system_clock::now();
			// converts time_point value to std::time_t
			const auto now_c = system_clock::to_time_t(now);
			std::tm now_parts{};
			// converts time_t value pointed by now_parts into a calendar time and stores it in now_c.
			localtime_s(&now_parts, &now_c); // TODO: might need to swap these variables

			if (now_parts.tm_hour >= end.count()) //end.count() returns number of ticks for duration 'end'
				now_parts.tm_mday += 1;

			now_parts.tm_hour = end.count();
			now_parts.tm_min = 0;
			now_parts.tm_sec = 0;

			auto next = system_clock::from_time_t(mktime(&now_parts));
			auto till = next - now + milliseconds(100);

			{
				// It might be more appropriate to use a lock_guard here?  less overhead and the lock's use is pretty simple
				std::unique_lock<std::mutex> ordersLock{ ordersMutex_ };

				if (shutdown_.load(std::memory_order_acquire) or // if atomic variable shutdown has been signaled to true to stop the application
					shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout) // or if condition variable  was awakened by notify_all, notify_one or spuriously
					return;  // return and do nothing, app is terminating.

				// lock is released at the end of this scope by destructor
			}

			OrderIds orderIds;
			{
				std::scoped_lock<std::mutex> ordersLock{ ordersMutex_ };

				// <OrderId, OrderEntry>
				for (const auto& [_, entry] : orders_)
				{
					// <OrderPointer, OrderPointers::iterator>
					const auto& [order, _] = entry;

					if (order->GetOrderType() != OrderType::GoodForDay)
						continue;

					orderIds.push_back(order->GetOrderId());
				}

				// lock is unlocked at end of this scope by destructor
			}

			CancelOrders(orderIds);
		}
	}

	bool OrderBook::CanFullyFill(Side side, Price price, Quantity quantity) const
	{
		if (!CanMatch(side, price))
			return false;

		std::optional<Price> threshold{};
		
		// If order is a buy, you want to set threshold to the LOWEST sell price
		if (side == Side::Buy)
		{
			const auto [askPrice, _] = *asks_.begin();
			threshold = askPrice;
		}
		// If order is a sell, you want to set threshold to the HIGHEST buy price
		else
		{
			const auto [bidPrice, _] = *bids_.begin();
			threshold = bidPrice;
		}

		for (const auto& [levelPrice, levelData] : data_)
		{
			// This if statement doesn't make sense to me yet, and I suspect he will change them in part three
			// e.g. for a buy order, the lowest sell price being greater than the current price level of the bookkeeping container should not be an issue,
			//   i.e, the current bookkeeping price level is a price that is CHEAPER, which is good for a buy order.
			if (threshold.has_value() and
				(side == Side::Buy and threshold.value() > levelPrice) or
				(side == Side::Sell and threshold.value() < levelPrice))
				continue;

			/* If the order is a buy order and the current price level in the bookkeeping is greater than the price of interest of the order (because you don't want to buy more expensive)
			*    OR
			*  If the order is a sell order and the current price level in the bookkeeping is less than the price of interest of the order (because you don't want to sell more cheap)
			* Then we do not care about this price level and we continue looping onto the next.
			*/
			if((side == Side::Buy and levelPrice > price) or
				(side == Side::Sell and levelPrice < price))
				continue;

			if (quantity <= levelData.quantity_)
				return true;

			quantity -= levelData.quantity_;
		}

		return false;
	}

	
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

				OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
				OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
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

	void OrderBook::CancelOrders(OrderIds orderIds)
	{
		// why scoped_lock here?
		// mutex is only acquired once, regardless of the number of orders in orderIds
		std::scoped_lock<std::mutex> ordersLock{ ordersMutex_ };

		for (const auto& orderId : orderIds)
			CancelOrderInternal(orderId);
	}

	/*
	* We refactor our previous CancelOrder function into a private CancelOrderInternal for the sake of avoiding
	*	owning and releasing locks multiple times, which leads to cache incoherence/inefficiency.
	*/
	void OrderBook::CancelOrderInternal(OrderId orderId)
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

		OnOrderCancelled(order);
	}

	void OrderBook::OnOrderCancelled(OrderPointer order)
	{
		UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
	}
	
	void OrderBook::OnOrderAdded(OrderPointer order)
	{
		UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
	}

	void OrderBook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
	{
		UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
	}

	void OrderBook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
	{
		auto& data = data_[price];

		data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;
		if (action == LevelData::Action::Remove or action == LevelData::Action::Match)
		{
			data.quantity_ -= quantity;
		}
		else
		{
			data.quantity_ += quantity;
		}

		if (data.count_ == 0)
			data_.erase(price);
	}
	

	/*******************************************************************
	*							Public API							   *
	********************************************************************/

	OrderBook::OrderBook()
		: ordersPruneThread_{ [this]() { PruneGoodForDayOrders(); } }
	{ }

	OrderBook::~OrderBook()
	{
		shutdown_.store(true, std::memory_order_release);
		shutdownConditionVariable_.notify_one();
		ordersPruneThread_.join();
	}

	Trades OrderBook::AddOrder(OrderPointer order)
	{
		std::scoped_lock<std::mutex> ordersLock{ ordersMutex_ };

		/* Exit condition */
		if (orders_.contains(order->GetOrderId()))
			return { };
		
		/********* Market Orders **********/
		// We leverage our GoodTillCancel Orders to implement Market orders
		if (order->GetOrderType() == OrderType::Market)
		{
			if (order->GetSide() == Side::Buy and !asks_.empty())
			{
				const auto& [worstAsk, _] = *asks_.rbegin();
				order->ToGoodTillCancel(worstAsk);
			}
			else if (order->GetSide() == Side::Sell and !bids_.empty())
			{
				const auto& [worstBid, _] = *bids_.rbegin();
				order->ToGoodTillCancel(worstBid);
			}
			else
				return { };
		}

		/********* FillAndKill orders **********/
		if (order->GetOrderType() == OrderType::FillAndKill and !CanMatch(order->GetSide(), order->GetPrice()))
			return { };

		/********* FillOrKill orders **********/
		if (order->GetOrderType() == OrderType::FillOrKill and !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
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

		OnOrderAdded(order);
		
		return MatchOrders();
	}

	void OrderBook::CancelOrder(OrderId orderId)
	{
		// Why scoped_lock here?
		std::scoped_lock<std::mutex> orderLocks{ ordersMutex_ };

		CancelOrderInternal(orderId);
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