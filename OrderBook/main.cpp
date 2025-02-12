#include "iostream"
#include "api/obOrderBook.hpp"

int main()
{
	// simple test to check that orders are being added and deleted from orderbook.
	ob::OrderBook orderbook{};
	const ob::OrderId orderId{ 1 };
	orderbook.AddOrder(std::make_shared<ob::Order>(ob::OrderType::GoodTillCancel, orderId, ob::Side::Buy, 100, 10));
	std::cout << orderbook.Size() << std::endl;
	orderbook.CancelOrder(orderId);
	std::cout << orderbook.Size() << std::endl;
	
	return 0;
}