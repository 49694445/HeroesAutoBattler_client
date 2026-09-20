/*
 * ArenaDeploymentMarketRules.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "PacksForServer.h"
#include <set>

VCMI_LIB_NAMESPACE_BEGIN

/// Artifact sales use instance IDs, not artifact type IDs or resource IDs.
inline bool isArenaDeploymentArtifactSale(const TradeOnMarketplace & pack)
{
	if(pack.mode != EMarketMode::ARTIFACT_RESOURCE || pack.r1.empty()
		|| pack.r1.size() != pack.r2.size() || pack.r1.size() != pack.val.size())
		return false;
	std::set<ArtifactInstanceID> instances;
	for(size_t index = 0; index < pack.r1.size(); ++index)
	{
		if(!pack.r1[index].is<ArtifactInstanceID>() || !pack.r2[index].is<GameResID>())
			return false;
		const auto resource = pack.r2[index].as<GameResID>();
		if(resource.getNum() < GameResID(EGameResID::WOOD).getNum()
			|| resource.getNum() > GameResID(EGameResID::GOLD).getNum()
			|| !instances.insert(pack.r1[index].as<ArtifactInstanceID>()).second)
			return false;
	}
	return true;
}

/// Deployment exposes the normal resource exchange market, including buying
/// resources with gold and exchanging two non-gold resources. Other market
/// modes remain ordinary-map actions.
inline bool isArenaDeploymentResourceTrade(GameResID resourceToSell, GameResID resourceToBuy)
{
	return resourceToSell.getNum() >= GameResID(EGameResID::WOOD).getNum()
		&& resourceToSell.getNum() <= GameResID(EGameResID::GOLD).getNum()
		&& resourceToBuy.getNum() >= GameResID(EGameResID::WOOD).getNum()
		&& resourceToBuy.getNum() <= GameResID(EGameResID::GOLD).getNum()
		&& resourceToSell != resourceToBuy;
}

/// Validate packet shape as well as direction. This is shared by the query
/// gate and the authoritative visitor so a malformed variant cannot be
/// mistaken for the default resource identifier.
inline bool isArenaDeploymentResourceTrade(const TradeOnMarketplace & pack)
{
	if(pack.mode != EMarketMode::RESOURCE_RESOURCE || pack.r1.empty()
		|| pack.r1.size() != pack.r2.size() || pack.r1.size() != pack.val.size())
		return false;

	for(size_t index = 0; index < pack.r1.size(); ++index)
	{
		if(!pack.r1[index].is<GameResID>() || !pack.r2[index].is<GameResID>() || pack.val[index] == 0)
			return false;
		if(!isArenaDeploymentResourceTrade(pack.r1[index].as<GameResID>(), pack.r2[index].as<GameResID>()))
			return false;
	}

	return true;
}

VCMI_LIB_NAMESPACE_END
