#pragma once

// lib
#include <cstdint>
#include <memory>
#include <list>
#include <vector>

namespace ob
{
	using Price = std::int32_t;
	using Quantity = std::uint32_t;
	using OrderId = std::uint64_t;
	using OrderIds = std::vector<OrderId>;

	struct LevelInfo;
	using LevelInfos = std::vector<LevelInfo>;

	class Order;
	using OrderPointer = std::shared_ptr<Order>;
	using OrderPointers = std::list<OrderPointer>;

	class Trade;
	using Trades = std::vector<Trade>;
}