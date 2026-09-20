/*
 * ArenaDeploymentDraftRules.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../ResourceSet.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

VCMI_LIB_NAMESPACE_BEGIN

namespace arena
{

struct ArenaDeploymentDraftBudget final
{
	TResources totalCost;
	bool valid = false;
	bool affordable = false;
};

/// One draft response can select several individually affordable items. Price
/// them together before the first item is granted, so ordering cannot overspend.
inline ArenaDeploymentDraftBudget evaluateArenaDeploymentDraftBudget(
	const TResources & resources, std::span<const TResources> selectedCosts)
{
	ArenaDeploymentDraftBudget result;
	size_t resourceCount = 0;
	for(const auto & cost : selectedCosts)
		resourceCount = std::max(resourceCount, cost.size());

	for(size_t resource = 0; resource < resourceCount; ++resource)
	{
		int64_t total = 0;
		for(const auto & cost : selectedCosts)
		{
			const int64_t value = cost[resource];
			if(value < 0 || value > std::numeric_limits<int64_t>::max() - total)
				return result;
			total += value;
		}
		if(total > std::numeric_limits<TResources::value_type>::max())
			return result;
		result.totalCost[resource] = static_cast<TResources::value_type>(total);
	}

	result.valid = true;
	result.affordable = resources.canAfford(result.totalCost);
	return result;
}

}

VCMI_LIB_NAMESPACE_END
