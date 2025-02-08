#pragma once

#include "api/obAliases.hpp"
#include "api/obSide.hpp"
#include "api/obOrderType.hpp"

namespace ob
{
	class OrderModify
	{
	public:
		OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
			: orderId_{ orderId }
			, side_{ side }
			, price_{ price }
			, quantity_{ quantity }
		{ }

		// Getters
		OrderId GetOrderId() const { return orderId_; }
		Price GetPrice() const { return price_; }
		Side GetSide() const { return side_; }
		Quantity GetQuantity() const { return quantity_; }

		OrderPointer ToOrderPointer(OrderType type) const
		{
			return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
		}

	private:
		OrderId orderId_;
		Side side_;
		Price price_;
		Quantity quantity_;
	};
}