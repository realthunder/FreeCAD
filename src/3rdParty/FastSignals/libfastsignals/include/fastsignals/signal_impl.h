#pragma once

#include "function_detail.h"
#include "spin_mutex.h"
#include <memory>
#include <vector>

namespace fastsignals::detail
{

class signal_impl
{
public:
	// [FreeCAD fork addition] The id space is split so that a slot can also be
	// added ahead of every existing one, which Boost.Signals2 spells
	// connect(slot, at_front). Everything here relies on m_ids being sorted
	// ascending, so front slots count DOWN from id_base and ordinary slots
	// count UP from it: a new front id is smaller than every id already
	// present, which is what makes the insert at the beginning keep the array
	// sorted. Nothing else in the class changes -- remove() and
	// get_next_slot() only ever assumed "sorted", never "starts at 1".
	static constexpr uint64_t id_base = uint64_t(1) << 32;

	uint64_t add(packed_function fn);

	// [FreeCAD fork addition] Adds a slot called before every existing one.
	uint64_t add_front(packed_function fn);

	void remove(uint64_t id) noexcept;

	void remove_all() noexcept;

	size_t count() const noexcept;

	template <class Combiner, class Result, class Signature, class... Args>
	Result invoke(Args... args) const
	{
		packed_function slot;
		size_t slotIndex = 0;
		uint64_t slotId = 1;

		if constexpr (std::is_same_v<Result, void>)
		{
			while (get_next_slot(slot, slotIndex, slotId))
			{
				slot.get<Signature>()(std::forward<Args>(args)...);
			}
		}
		else
		{
			Combiner combiner;
			while (get_next_slot(slot, slotIndex, slotId))
			{
				combiner(slot.get<Signature>()(std::forward<Args>(args)...));
			}
			return combiner.get_value();
		}
	}

private:
	bool get_next_slot(packed_function& slot, size_t& expectedIndex, uint64_t& nextId) const;

	mutable spin_mutex m_mutex;
	std::vector<packed_function> m_functions;
	std::vector<uint64_t> m_ids;
	uint64_t m_nextId = id_base;
	uint64_t m_nextFrontId = id_base;
};

using signal_impl_ptr = std::shared_ptr<signal_impl>;
using signal_impl_weak_ptr = std::weak_ptr<signal_impl>;

} // namespace fastsignals::detail
