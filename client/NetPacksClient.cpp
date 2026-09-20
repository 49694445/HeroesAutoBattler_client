/*
 * NetPacksClient.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ClientNetPackVisitors.h"

#include "Client.h"
#include "CPlayerInterface.h"
#include "windows/GUIClasses.h"
#include "windows/InfoWindows.h"
#include "windows/CCastleInterface.h"
#include "windows/CHeroWindow.h"
#include "windows/ArenaArtifactDraftWindow.h"
#include "windows/CCreatureWindow.h"
#include "mapView/mapHandler.h"
#include "mainmenu/CMainMenu.h"
#include "adventureMap/AdventureMapInterface.h"
#include "adventureMap/CInGameConsole.h"
#include "battle/BattleInterface.h"
#include "battle/BattleWindow.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "gui/WindowHandler.h"
#include "widgets/MiscWidgets.h"
#include "widgets/CComponent.h"
#include "widgets/TextControls.h"
#include "CMT.h"
#include "GameChatHandler.h"
#include "CServerHandler.h"
#include "UIHelper.h"

#include "../lib/callback/CCallback.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/filesystem/FileInfo.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/entities/ResourceTypeHandler.h"
#include "../lib/mapping/CMap.h"
#include "../lib/VCMIDirs.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/CSoundBase.h"
#include "../lib/StartInfo.h"
#include "../lib/CConfigHandler.h"
#include "../lib/mapObjects/MiscObjects.h"
#include "../lib/mapObjects/CGMarket.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/json/JsonNode.h"
#include "../lib/CStack.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/GameConstants.h"
#include "../lib/CPlayerState.h"

namespace
{
class ArenaDailyResourceWindow;
std::weak_ptr<ArenaDailyResourceWindow> arenaDailyResourceWindow;

class ArenaDailyResourceWindow final : public CInfoWindow
{
	int32_t remainingMilliseconds;
	bool dismissed = false;

public:
	ArenaDailyResourceWindow(PlayerColor player, const TResources & resources, int32_t durationSeconds)
		: CInfoWindow(LIBRARY->generaltexth->translate("vcmi.arena.dailyResources.title"), player, makeComponents(resources), {})
		, remainingMilliseconds(std::max(0, durationSeconds) * 1000)
	{
		addUsedEvents(TIME);
	}

	static TCompsInfo makeComponents(const TResources & resources)
	{
		TCompsInfo result;
		for(const auto resource : LIBRARY->resourceTypeHandler->getAllObjects())
			if(resources[resource] != 0)
				result.push_back(std::make_shared<CComponent>(ComponentType::RESOURCE, resource, resources[resource], CComponent::large));
		return result;
	}

	void close() override
	{
		// No button, Escape, right-click, or other local action may dismiss this
		// server-sequenced deployment modal.
	}

	void dismiss()
	{
		if(dismissed)
			return;
		dismissed = true;
		CInfoWindow::close();
	}

	void tick(uint32_t msPassed) override
	{
		remainingMilliseconds = std::max(0, remainingMilliseconds - static_cast<int32_t>(msPassed));
		if(remainingMilliseconds == 0)
			dismiss();
	}
};

void clearArenaDailyResourceWindow()
{
	if(auto window = arenaDailyResourceWindow.lock())
		window->dismiss();
	arenaDailyResourceWindow.reset();
}

std::string makeArenaCommandChannelClientBattleSnapshot(const CGameState & gameState, PlayerColor target)
{
	JsonNode root;
	root["source"].String() = "client";
	root["player"].String() = target.toString();
	root["day"].Integer() = gameState.day;
	root["deploymentActive"].Bool() = GAME->interface() && GAME->interface()->playerID == target
		&& GAME->interface()->arenaDeploymentActive.load();

	const auto * battle = gameState.getBattle(target);
	root["battleActive"].Bool() = battle != nullptr;
	if(!battle)
		return root.toString();

	const TerrainId terrain = battle->getTerrainType();
	root["battle"]["id"].Integer() = battle->getBattleID().getNum();
	root["battle"]["terrain"]["id"].Integer() = terrain.getNum();
	root["battle"]["terrain"]["identifier"].String() = TerrainId::encode(terrain.getNum());
	root["battle"]["siege"].Bool() = battle->getDefendedTown() != nullptr;

	const auto nativeSource = Selector::sourceTypeSel(BonusSource::TERRAIN_NATIVE);
	for(const auto * stack : battle->battleGetAllStacks(true))
	{
		if(!stack)
			continue;
		JsonNode entry;
		const auto owner = battle->sideToPlayer(stack->unitSide());
		const auto creature = stack->unitType()->getId();
		const TerrainId nativeTerrain = stack->getNativeTerrain();
		entry["unitId"].Integer() = stack->unitId();
		entry["player"].String() = owner.toString();
		entry["side"].Integer() = static_cast<int32_t>(stack->unitSide());
		entry["creature"]["id"].Integer() = creature.getNum();
		entry["creature"]["identifier"].String() = stack->unitType()->getJsonKey();
		entry["count"].Integer() = stack->getCount();
		entry["alive"].Bool() = stack->alive();
		entry["arenaInitialUnit"].Bool() = stack->base && stack->base->isArenaInitialUnit();
		entry["nativeTerrain"]["id"].Integer() = nativeTerrain.getNum();
		entry["nativeTerrain"]["identifier"].String() = TerrainId::encode(nativeTerrain.getNum());
		entry["isOnNativeTerrain"].Bool() = nativeTerrain == TerrainId::ANY_TERRAIN || nativeTerrain == terrain;
		entry["actual"]["attack"].Integer() = stack->getAttack(false);
		entry["actual"]["defense"].Integer() = stack->getDefense(false);
		entry["actual"]["speed"].Integer() = stack->getInitiative(0);
		entry["nativeTerrainBonus"]["attack"].Integer() = stack->valOfBonuses(
			Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)).And(nativeSource));
		entry["nativeTerrainBonus"]["defense"].Integer() = stack->valOfBonuses(
			Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)).And(nativeSource));
		entry["nativeTerrainBonus"]["speed"].Integer() = stack->valOfBonuses(
			Selector::type()(BonusType::STACKS_SPEED).And(nativeSource));
		root["battle"]["stacks"].Vector().push_back(std::move(entry));
	}
	return root.toString();
}

class ArenaDisconnectWaitWindow;
std::weak_ptr<ArenaDisconnectWaitWindow> arenaDisconnectWaitWindow;

void scheduleArenaDisconnectWaitDismissal();

class ArenaDisconnectWaitWindow final : public CInfoWindow
{
	bool dismissRequested = false;

public:
	ArenaDisconnectWaitWindow(const std::string & text, PlayerColor player)
		: CInfoWindow(text, player, {}, {})
	{
	}

	void close() override
	{
		// This server-authoritative modal is closed only by a terminal state pack.
	}

	void activate() override
	{
		CInfoWindow::activate();
		if(dismissRequested)
			scheduleArenaDisconnectWaitDismissal();
	}

	void updateText(const std::string & value)
	{
		text->setText(value);
		ENGINE->windows().totalRedraw();
	}

	void dismiss()
	{
		CInfoWindow::close();
	}

	void requestDismissal()
	{
		dismissRequested = true;
		scheduleArenaDisconnectWaitDismissal();
	}

	bool isDismissRequested() const
	{
		return dismissRequested;
	}
};

void scheduleArenaDisconnectWaitDismissal()
{
	ENGINE->dispatchMainThread([]()
	{
		auto window = arenaDisconnectWaitWindow.lock();
		if(!window || !window->isDismissRequested())
			return;

		if(ENGINE->windows().isTopWindow(window))
		{
			window->dismiss();
			arenaDisconnectWaitWindow.reset();
			return;
		}

		if(ENGINE->windows().findWindows<ArenaDisconnectWaitWindow>().empty())
			arenaDisconnectWaitWindow.reset();
	});
}

std::string arenaDisconnectWaitText(int32_t remainingSeconds)
{
	return boost::str(boost::format(LIBRARY->generaltexth->translate("vcmi.arena.disconnect.wait")) % std::max(0, remainingSeconds));
}
}

void clearArenaDisconnectWaitWindow()
{
	if(settings["session"]["headless"].Bool() || !ENGINE)
	{
		arenaDisconnectWaitWindow.reset();
		return;
	}
	ENGINE->dispatchMainThread([]()
	{
		if(auto window = arenaDisconnectWaitWindow.lock())
			window->requestDismissal();
		else
			arenaDisconnectWaitWindow.reset();
	});
}

// TODO: as Tow suggested these template should all be part of CClient
// This will require rework spectator interface properly though

template<typename T, typename ... Args, typename ... Args2>
bool callOnlyThatInterface(CClient & cl, PlayerColor player, void (T::*ptr)(Args...), Args2 && ...args)
{
	if(vstd::contains(cl.playerint, player))
	{
		((*cl.playerint[player]).*ptr)(std::forward<Args2>(args)...);
		return true;
	}
	return false;
}

template<typename T, typename ... Args, typename ... Args2>
bool callInterfaceIfPresent(CClient & cl, PlayerColor player, void (T::*ptr)(Args...), Args2 && ...args)
{
	bool called = callOnlyThatInterface(cl, player, ptr, std::forward<Args2>(args)...);
	return called;
}

template<typename T, typename ... Args, typename ... Args2>
void callOnlyThatBattleInterface(CClient & cl, PlayerColor player, void (T::*ptr)(Args...), Args2 && ...args)
{
	if(vstd::contains(cl.battleints,player))
		((*cl.battleints[player]).*ptr)(std::forward<Args2>(args)...);

	if(cl.additionalBattleInts.count(player))
	{
		for(auto bInt : cl.additionalBattleInts[player])
			((*bInt).*ptr)(std::forward<Args2>(args)...);
	}
}

template<typename T, typename ... Args, typename ... Args2>
void callBattleInterfaceIfPresent(CClient & cl, PlayerColor player, void (T::*ptr)(Args...), Args2 && ...args)
{
	callOnlyThatInterface(cl, player, ptr, std::forward<Args2>(args)...);
}

//calls all normal interfaces and privileged ones, playerints may be updated when iterating over it, so we need a copy
template<typename T, typename ... Args, typename ... Args2>
void callAllInterfaces(CClient & cl, void (T::*ptr)(Args...), Args2 && ...args)
{
	for(auto pInt : cl.playerint)
	{
		// Spectators have no adventure-map PlayerState. Battle events are routed
		// through battleints below; broadcasting ordinary player events to the
		// spectator would call hero/pathfinding code on nonexistent player data.
		if(pInt.first == PlayerColor::SPECTATOR)
			continue;
		((*pInt.second).*ptr)(std::forward<Args2>(args)...);
	}
}

//calls all normal interfaces and privileged ones, playerints may be updated when iterating over it, so we need a copy
template<typename T, typename ... Args, typename ... Args2>
void callBattleInterfaceIfPresentForBothSides(CClient & cl, const BattleID & battleID, void (T::*ptr)(Args...), Args2 && ...args)
{
	assert(cl.gameState().getBattle(battleID));

	if(!cl.gameState().getBattle(battleID))
	{
		logGlobal->error("Attempt to call battle interface without ongoing battle!");
		return;
	}

	callOnlyThatBattleInterface(cl, cl.gameState().getBattle(battleID)->getSide(BattleSide::ATTACKER).color, ptr, std::forward<Args2>(args)...);
	callOnlyThatBattleInterface(cl, cl.gameState().getBattle(battleID)->getSide(BattleSide::DEFENDER).color, ptr, std::forward<Args2>(args)...);
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool() && GAME->interface()->battleInt)
	{
		callOnlyThatBattleInterface(cl, PlayerColor::SPECTATOR, ptr, std::forward<Args2>(args)...);
	}
}

void ApplyClientNetPackVisitor::visitSetResources(SetResources & pack)
{
	//todo: inform on actual resource set transferred
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::receivedResource);
}

void ApplyClientNetPackVisitor::visitSetArenaHealth(SetArenaHealth & pack)
{
	cl.updateArenaHealth(pack);
}

void ApplyClientNetPackVisitor::visitSetArenaHeroLevelUpChoiceTimeout(SetArenaHeroLevelUpChoiceTimeout & pack)
{
	if(!GAME->interface() || GAME->interface()->playerID != pack.player)
		return;

	GAME->interface()->setArenaHeroLevelUpChoiceTimeout(pack.queryID, pack.remainingSeconds);
}

void ApplyClientNetPackVisitor::visitSetArenaBattleResultTimeout(SetArenaBattleResultTimeout & pack)
{
	if(!GAME->interface() || GAME->interface()->playerID != pack.player)
		return;

	GAME->interface()->setArenaBattleResultTimeout(pack.queryID, pack.remainingSeconds);
}

void ApplyClientNetPackVisitor::visitSetArenaSummonsRemaining(SetArenaSummonsRemaining & pack)
{
	if(!ENGINE)
		return;
	const auto player = pack.player;
	ENGINE->dispatchMainThread([player]()
	{
		auto * interface = GAME->interface();
		if(!interface || interface->playerID != player)
			return;

		for(auto window : ENGINE->windows().findWindows<CCastleInterface>())
			window->refreshArenaSummonsRemaining();
	});
}

void ApplyClientNetPackVisitor::visitArenaDeploymentStarted(ArenaDeploymentStarted & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaDeploymentState, static_cast<const ArenaDeploymentStarted &>(pack));
	if(!GAME->interface() || GAME->interface()->playerID != pack.player)
		return;

	GAME->interface()->setArenaDeploymentActive(pack.active);
	if(pack.active)
	{
		GAME->interface()->setArenaNextBattleTerrain(pack.nextBattleTerrain);
		GAME->interface()->setArenaNextBattleSiege(pack.decisiveBattle);
		GAME->interface()->setArenaLastOrdinaryBattleLoss(pack.lastOrdinaryBattleLoser, pack.consecutiveOrdinaryLosses, pack.siegeInterval);
	}

	const auto player = GAME->interface()->playerID;
	const bool active = pack.active;
	const auto previousOwnHero = pack.previousOwnHero;
	const auto previousEnemyHero = pack.previousEnemyHero;
	ENGINE->dispatchMainThread([player, active, previousOwnHero, previousEnemyHero]()
	{
		auto * current = GAME->interface();
		if(!current || current->playerID != player)
			return;
		if(active)
			current->setArenaPreviousBattleHeroes(previousOwnHero, previousEnemyHero);
		if(!adventureInt)
			return;
		adventureInt->refreshArenaNextBattleTerrainLabel();
		adventureInt->refreshArenaPreviousEnemyHeroPortrait();
		if(!active)
			return;
		if(current->castleInt || CPlayerInterface::battleInt)
			return;
		adventureInt->onAudioResumed();
	});
}

void ApplyClientNetPackVisitor::visitArenaNativeProbeResult(ArenaNativeProbeResult & pack)
{
	callInterfaceIfPresent(cl,pack.player,&IGameEventsReceiver::arenaNativeProbeResult,static_cast<const ArenaNativeProbeResult &>(pack));
}

void ApplyClientNetPackVisitor::visitArenaArtifactDraftSelection(ArenaArtifactDraftSelection & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaArtifactDraftState, static_cast<const ArenaArtifactDraftSelection &>(pack));
	if(!GAME->interface() || GAME->interface()->playerID != pack.player)
		return;
	GAME->interface()->arenaArtifactDraftActive.store(pack.active);
	if(!pack.active)
	{
		ENGINE->dispatchMainThread([](){ clearArenaArtifactDraftWindow(); });
		return;
	}

	const auto selection = pack;
	ENGINE->dispatchMainThread([selection]()
	{
		if(!GAME->interface() || GAME->interface()->playerID != selection.player)
			return;
		showArenaArtifactDraftWindow(selection);
	});
}

void ApplyClientNetPackVisitor::visitArenaDailyResourceGrant(ArenaDailyResourceGrant & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaDailyResourceState, static_cast<const ArenaDailyResourceGrant &>(pack));
	if(!GAME->interface() || GAME->interface()->playerID != pack.player)
		return;
	GAME->interface()->arenaDailyResourcePresentationActive.store(pack.active && pack.durationSeconds > 0);
	const auto grant = pack;
	ENGINE->dispatchMainThread([grant]()
	{
		if(!GAME->interface() || GAME->interface()->playerID != grant.player)
			return;
		clearArenaDailyResourceWindow();
		if(!grant.active || grant.durationSeconds <= 0 || grant.resources.empty())
			return;
		auto window = std::make_shared<ArenaDailyResourceWindow>(grant.player, grant.resources, grant.durationSeconds);
		arenaDailyResourceWindow = window;
		ENGINE->windows().pushWindow(window);
	});
}

void ApplyClientNetPackVisitor::visitArenaDisconnectWaitState(ArenaDisconnectWaitState & pack)
{
	const auto status = pack.status;
	const auto remainingSeconds = pack.remainingSeconds;
	ENGINE->dispatchMainThread([status, remainingSeconds]()
	{
		auto window = arenaDisconnectWaitWindow.lock();

		if(status != EArenaDisconnectWaitStatus::WAITING)
		{
			clearArenaDisconnectWaitWindow();
			return;
		}

		if(!GAME->interface())
			return;

		const auto text = arenaDisconnectWaitText(remainingSeconds);
		if(window)
		{
			if(!window->isDismissRequested())
				window->updateText(text);
			return;
		}

		window = std::make_shared<ArenaDisconnectWaitWindow>(text, GAME->interface()->playerID);
		arenaDisconnectWaitWindow = window;
		ENGINE->windows().pushWindow(window);
	});
}

void ApplyClientNetPackVisitor::visitArenaCommandChannelClientRequest(ArenaCommandChannelClientRequest & pack)
{
	const auto requestID = pack.requestID;
	const auto target = pack.target;
	const auto query = pack.query;
	ENGINE->dispatchMainThread([requestID, target, query]()
	{
		if(!GAME->interface() || !GAME->server().client || GAME->interface()->playerID != target)
			return;

		ArenaCommandChannelClientResponse response;
		response.requestID = requestID;
		response.query = query;
		try
		{
			if(query != "battle")
			{
				JsonNode error;
				error["source"].String() = "client";
				error["error"].String() = "unsupported query";
				response.payload = error.toString();
			}
			else
			{
				response.payload = makeArenaCommandChannelClientBattleSnapshot(GAME->server().client->gameState(), target);
			}
		}
		catch(const std::exception & exception)
		{
			JsonNode error;
			error["source"].String() = "client";
			error["error"].String() = exception.what();
			response.payload = error.toString();
		}
		GAME->server().client->sendRequest(response, target, false);
	});
}

void ApplyClientNetPackVisitor::visitSetArenaTownVisitState(SetArenaTownVisitState & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaTownVisitState, static_cast<const SetArenaTownVisitState &>(pack));
	if(GAME->interface())
		GAME->interface()->applyArenaTownVisitState(pack);
}

void ApplyClientNetPackVisitor::visitSetArenaTownBuildGateState(SetArenaTownBuildGateState & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaTownBuildGateState, static_cast<const SetArenaTownBuildGateState &>(pack));
	if(GAME->interface())
		GAME->interface()->applyArenaTownBuildGateState(pack);
}

void ApplyClientNetPackVisitor::visitSetArenaMainHero(SetArenaMainHero & pack)
{
	if(!ENGINE)
		return;
	const auto player = pack.player;
	ENGINE->dispatchMainThread([player]()
	{
		auto * current = GAME->interface();
		if(!current || current->playerID != player)
			return;
		for(const auto & window : ENGINE->windows().findWindows<CHeroWindow>())
			window->refreshArenaMainHeroMarkers();
	});
}

void ApplyClientNetPackVisitor::visitSetArenaBuildLevelState(SetArenaBuildLevelState & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaBuildLevelState, static_cast<const SetArenaBuildLevelState &>(pack));
	const auto state = pack;
	logGlobal->debug("[ARENA] received build-level state town=%d building=%d revision=%u status=%d", state.town.getNum(), state.building.getNum(), state.revision, static_cast<int>(state.status));
	if(!ENGINE)
		return;
	ENGINE->dispatchMainThread([state]()
	{
		if(!GAME->interface() || state.player != GAME->interface()->playerID)
			return;
		auto windows = ENGINE->windows().findWindows<CBuildWindow>();
		if(windows.empty())
		{
			logGlobal->debug("[ARENA] dropping build-level state without matching window town=%d building=%d revision=%u status=%d", state.town.getNum(), state.building.getNum(), state.revision, static_cast<int>(state.status));
			return;
		}
		for(auto window : windows)
			window->applyArenaBuildLevelState(state);
	});
}

void ApplyClientNetPackVisitor::visitSetArenaTownSummonState(SetArenaTownSummonState & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::arenaTownSummonState, static_cast<const SetArenaTownSummonState &>(pack));
	if(!ENGINE)
		return;
	const SetArenaTownSummonState state = pack;
	// Network packet handling holds the interface mutex. Keep this stage limited to
	// copying the packet and enqueueing one main-thread transaction; it must not
	// inspect or mutate GUI objects here.
	ENGINE->dispatchMainThread([state]()
	{
		if(GAME->interface())
			GAME->interface()->applyArenaTownSummonState(state);
	});
}

void ApplyClientNetPackVisitor::visitSetArenaBattleCreatureProgress(SetArenaBattleCreatureProgress & pack)
{
	cl.updateArenaBattleRecruitmentThresholds(pack);
	if(GAME->interface())
		GAME->interface()->applyArenaBattleCreatureProgress(pack);
}

void ApplyClientNetPackVisitor::visitSetHeroExperience(SetHeroExperience & pack)
{
	const CGHeroInstance * h = cl.gameInfo().getHero(pack.id);
	if(!h)
	{
		logNetwork->error("Cannot find hero with pack.id %d", pack.id.getNum());
		return;
	}
	callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroExperienceChanged, h, pack.val);
}

void ApplyClientNetPackVisitor::visitSetPrimarySkill(SetPrimarySkill & pack)
{
	const CGHeroInstance * h = cl.gameInfo().getHero(pack.id);
	if(!h)
	{
		logNetwork->error("Cannot find hero with pack.id %d", pack.id.getNum());
		return;
	}
	callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroPrimarySkillChanged, h, pack.which, pack.val);
}

void ApplyClientNetPackVisitor::visitSetSecSkill(SetSecSkill & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.id);
	if(!h)
	{
		logNetwork->error("Cannot find hero with pack.id %d", pack.id.getNum());
		return;
	}
	callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroSecondarySkillChanged, h, pack.which, pack.val);
}

void ApplyClientNetPackVisitor::visitHeroVisitCastle(HeroVisitCastle & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.hid);
	
	if(pack.start())
	{
		callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroVisitsTown, h, gs.getTown(pack.tid));
	}
}

void ApplyClientNetPackVisitor::visitSetMana(SetMana & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.hid);
	callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroManaPointsChanged, h);

	if(settings["session"]["headless"].Bool())
		return;

	for(auto window : ENGINE->windows().findWindows<BattleWindow>())
		window->heroManaPointsChanged(h);
}

void ApplyClientNetPackVisitor::visitSetMovePoints(SetMovePoints & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.hid);
	callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroMovePointsChanged, h);
}

void ApplyClientNetPackVisitor::visitSetResearchedSpells(SetResearchedSpells & pack)
{
	if(!ENGINE)
		return;
	for(const auto & win : ENGINE->windows().findWindows<CMageGuildScreen>())
		win->updateSpells(pack.tid);
}

void ApplyClientNetPackVisitor::visitFoWChange(FoWChange & pack)
{
	for(auto &i : cl.playerint)
	{
		if(cl.gameInfo().getPlayerRelations(i.first, pack.player) == PlayerRelations::SAME_PLAYER && pack.waitForDialogs && GAME->interface() == i.second.get())
		{
			GAME->interface()->waitWhileDialog();
		}
		if(cl.gameInfo().getPlayerRelations(i.first, pack.player) != PlayerRelations::ENEMIES)
		{
			if(pack.mode == ETileVisibility::REVEALED)
				i.second->tileRevealed(pack.tiles);
			else
				i.second->tileHidden(pack.tiles);
		}
	}
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);
}

static void dispatchGarrisonChange(CClient & cl, ObjectInstanceID army1, ObjectInstanceID army2)
{
	auto obj1 = cl.gameInfo().getObj(army1);
	if(!obj1)
	{
		logNetwork->error("Cannot find army with pack.id %d", army1.getNum());
		return;
	}

	callInterfaceIfPresent(cl, obj1->tempOwner, &IGameEventsReceiver::garrisonsChanged, army1, army2);

	if(army2 != ObjectInstanceID() && army2 != army1)
	{
		auto obj2 = cl.gameInfo().getObj(army2);
		if(!obj2)
		{
			logNetwork->error("Cannot find army with pack.id %d", army2.getNum());
			return;
		}

		if(obj1->tempOwner != obj2->tempOwner)
			callInterfaceIfPresent(cl, obj2->tempOwner, &IGameEventsReceiver::garrisonsChanged, army1, army2);
	}
}

void ApplyClientNetPackVisitor::visitChangeStackCount(ChangeStackCount & pack)
{
	dispatchGarrisonChange(cl, pack.army, ObjectInstanceID());
}

void ApplyClientNetPackVisitor::visitSetStackType(SetStackType & pack)
{
	dispatchGarrisonChange(cl, pack.army, ObjectInstanceID());
}

void ApplyClientNetPackVisitor::visitEraseStack(EraseStack & pack)
{
	dispatchGarrisonChange(cl, pack.army, ObjectInstanceID());
}

void ApplyClientNetPackVisitor::visitSwapStacks(SwapStacks & pack)
{
	dispatchGarrisonChange(cl, pack.srcArmy, pack.dstArmy);
}

void ApplyClientNetPackVisitor::visitInsertNewStack(InsertNewStack & pack)
{
	dispatchGarrisonChange(cl, pack.army, ObjectInstanceID());
}

void ApplyClientNetPackVisitor::visitRebalanceStacks(RebalanceStacks & pack)
{
	dispatchGarrisonChange(cl, pack.srcArmy, pack.dstArmy);
}

void ApplyClientNetPackVisitor::visitBulkRebalanceStacks(BulkRebalanceStacks & pack)
{
	if(!pack.moves.empty())
	{
		auto destArmy = pack.moves[0].srcArmy == pack.moves[0].dstArmy
			? ObjectInstanceID()
			: pack.moves[0].dstArmy;
		dispatchGarrisonChange(cl, pack.moves[0].srcArmy, destArmy);
	}
}

void ApplyClientNetPackVisitor::visitPutArtifact(PutArtifact & pack)
{
	callInterfaceIfPresent(cl, cl.gameState().getOwner(pack.al.artHolder), &IGameEventsReceiver::artifactPut, pack.al);
	if(pack.askAssemble)
		callInterfaceIfPresent(cl, cl.gameState().getOwner(pack.al.artHolder), &IGameEventsReceiver::askToAssembleArtifact, pack.al);
}

void ApplyClientNetPackVisitor::visitBulkEraseArtifacts(BulkEraseArtifacts & pack)
{
	for(const auto & slotErase : pack.posPack)
		callInterfaceIfPresent(cl, cl.gameState().getOwner(pack.artHolder), &IGameEventsReceiver::artifactRemoved, ArtifactLocation(pack.artHolder, slotErase));
}

void ApplyClientNetPackVisitor::visitBulkMoveArtifacts(BulkMoveArtifacts & pack)
{
	const auto dstOwner = cl.gameState().getOwner(pack.dstArtHolder);
	const auto applyMove = [this, &pack, dstOwner](const std::vector<MoveArtifactInfo> & artsPack)
	{
		for(const auto & slotToMove : artsPack)
		{
			const auto srcLoc = ArtifactLocation(pack.srcArtHolder, slotToMove.srcPos);
			const auto dstLoc = ArtifactLocation(pack.dstArtHolder, slotToMove.dstPos);

			callInterfaceIfPresent(cl, pack.interfaceOwner, &IGameEventsReceiver::artifactMoved, srcLoc, dstLoc);
			if(slotToMove.askAssemble)
				callInterfaceIfPresent(cl, pack.interfaceOwner, &IGameEventsReceiver::askToAssembleArtifact, dstLoc);
			if(pack.interfaceOwner != dstOwner)
				callInterfaceIfPresent(cl, dstOwner, &IGameEventsReceiver::artifactMoved, srcLoc, dstLoc);
		}
	};

	size_t possibleAssemblyNumOfArts = 0;
	const auto calcPossibleAssemblyNumOfArts = [&possibleAssemblyNumOfArts](const auto & slotToMove)
	{
		if(slotToMove.askAssemble)
			possibleAssemblyNumOfArts++;
	};
	std::for_each(pack.artsPack0.cbegin(), pack.artsPack0.cend(), calcPossibleAssemblyNumOfArts);
	std::for_each(pack.artsPack1.cbegin(), pack.artsPack1.cend(), calcPossibleAssemblyNumOfArts);


	// Begin a session of bulk movement of arts. It is not necessary but useful for the client optimization.
	callInterfaceIfPresent(cl, pack.interfaceOwner, &IGameEventsReceiver::bulkArtMovementStart,
		pack.artsPack0.size() + pack.artsPack1.size(), possibleAssemblyNumOfArts);
	if(pack.interfaceOwner != dstOwner)
		callInterfaceIfPresent(cl, dstOwner, &IGameEventsReceiver::bulkArtMovementStart,
			pack.artsPack0.size() + pack.artsPack1.size(), possibleAssemblyNumOfArts);

	applyMove(pack.artsPack0);
	if(!pack.artsPack1.empty())
		applyMove(pack.artsPack1);
}

void ApplyClientNetPackVisitor::visitAssembledArtifact(AssembledArtifact & pack)
{
	callInterfaceIfPresent(cl, cl.gameState().getOwner(pack.al.artHolder), &IGameEventsReceiver::artifactAssembled, pack.al);
}

void ApplyClientNetPackVisitor::visitDisassembledArtifact(DisassembledArtifact & pack)
{
	callInterfaceIfPresent(cl, cl.gameState().getOwner(pack.al.artHolder), &IGameEventsReceiver::artifactDisassembled, pack.al);
}

void ApplyClientNetPackVisitor::visitHeroVisit(HeroVisit & pack)
{
	auto hero = cl.gameInfo().getHero(pack.heroId);
	auto obj = cl.gameInfo().getObj(pack.objId, false);
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::heroVisit, hero, obj, pack.starting);
}

void ApplyClientNetPackVisitor::visitNewTurn(NewTurn & pack)
{
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);

	if(pack.newWeekNotification && !cl.gameInfo().getMapHeader()->battleOnly)
	{
		const auto & newWeek = *pack.newWeekNotification;

		std::string str = newWeek.text.toString();
		callAllInterfaces(cl, &CGameInterface::showInfoDialog, newWeek.type, str, newWeek.components,(soundBase::soundID)newWeek.soundID);
	}
}

void ApplyClientNetPackVisitor::visitGiveBonus(GiveBonus & pack)
{
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);

	switch(pack.who)
	{
	case GiveBonus::ETarget::OBJECT:
		{
			const CGHeroInstance *h = gs.getHero(pack.id.as<ObjectInstanceID>());
			if(h)
				callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroBonusChanged, h, pack.bonus, true);
		}
		break;
	case GiveBonus::ETarget::PLAYER:
		{
			callInterfaceIfPresent(cl, pack.id.as<PlayerColor>(), &IGameEventsReceiver::playerBonusChanged, pack.bonus, true);
		}
		break;
	}
}

void ApplyFirstClientNetPackVisitor::visitChangeObjPos(ChangeObjPos & pack)
{
	const CGObjectInstance *obj = gs.getObjInstance(pack.objid);
	GAME->map().onObjectFadeOut(obj, pack.initiator);
	GAME->map().waitForOngoingAnimations();
}

void ApplyClientNetPackVisitor::visitChangeObjPos(ChangeObjPos & pack)
{
	const CGObjectInstance *obj = gs.getObjInstance(pack.objid);
	GAME->map().onObjectFadeIn(obj, pack.initiator);
	GAME->map().waitForOngoingAnimations();
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);
}

void ApplyClientNetPackVisitor::visitPlayerEndsGame(PlayerEndsGame & pack)
{
	callAllInterfaces(cl, &IGameEventsReceiver::gameOver, pack.player, pack.victoryLossCheckResult);

	bool localHumanWinsGame = vstd::contains(cl.playerint, pack.player) && cl.gameInfo().getPlayerState(pack.player)->human && pack.victoryLossCheckResult.victory();
	bool lastHumanEndsGame = GAME->server().howManyPlayerInterfaces() == 1 && vstd::contains(cl.playerint, pack.player) && cl.gameInfo().getPlayerState(pack.player)->human && !settings["session"]["spectate"].Bool();

	if(ENGINE && (lastHumanEndsGame || localHumanWinsGame || pack.silentEnd))
	{
		assert(adventureInt);
		if(adventureInt)
		{
			ENGINE->windows().popWindows(ENGINE->windows().count());
			adventureInt.reset();
		}

		if(!pack.silentEnd)
			GAME->server().showHighScoresAndEndGameplay(pack.player, pack.victoryLossCheckResult.victory(), pack.statistic);
		else
		{
			GAME->server().endGameplay();
			GAME->mainmenu()->menu->switchToTab("main");
		}
	}

	// In auto testing pack.mode we always close client if red pack.player won or lose
	if(!settings["session"]["testmap"].isNull() && pack.player == PlayerColor(0))
	{
		logAi->info("Red player %s. Ending game.", pack.victoryLossCheckResult.victory() ? "won" : "lost");

		// Network packs are applied on runNetwork. GameShutdownException is the
		// main-loop exit signal and must be thrown on MainGUI; throwing it here
		// escapes the network thread and terminates the process via std::terminate.
		const bool askForConfirmation = settings["session"]["spectate"].Bool();
		if(ENGINE)
			ENGINE->dispatchMainThread([askForConfirmation]()
			{
				GAME->onShutdownRequested(askForConfirmation);
			});
		else
			GAME->onShutdownRequested(false);
	}
}

void ApplyClientNetPackVisitor::visitRemoveBonus(RemoveBonus & pack)
{
	switch(pack.who)
	{
	case GiveBonus::ETarget::OBJECT:
		{
			const CGHeroInstance *h = gs.getHero(pack.whoID.as<ObjectInstanceID>());
			if(h)
				callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroBonusChanged, h, pack.bonus, false);
		}
		break;
	case GiveBonus::ETarget::PLAYER:
		{
			//const PlayerState *p = gs.getPlayerState(pack.id);
			callInterfaceIfPresent(cl, pack.whoID.as<PlayerColor>(), &IGameEventsReceiver::playerBonusChanged, pack.bonus, false);
		}
		break;
	}
}

void ApplyFirstClientNetPackVisitor::visitRemoveObject(RemoveObject & pack)
{
	const CGObjectInstance *o = cl.gameInfo().getObj(pack.objectID);
	const auto * h = dynamic_cast<const CGHeroInstance*>(o);

	GAME->map().onObjectFadeOut(o, pack.initiator);
	if (h && h->inBoat())
	{
		GAME->map().waitForOngoingAnimations();
		GAME->map().onObjectFadeOut(h->getBoat(), pack.initiator);
	}

	//notify interfaces about removal
	for(auto i=cl.playerint.begin(); i!=cl.playerint.end(); i++)
	{
		//below line contains little cheat for AI so it will be aware of deletion of enemy heroes that moved or got re-covered by FoW
		//TODO: loose requirements as next AI related crashes appear, for example another pack.player collects object that got re-covered by FoW, unsure if AI code workarounds this
		if(gs.isVisibleFor(o, i->first) || (!cl.gameInfo().getPlayerState(i->first)->human && o->ID == Obj::HERO && o->tempOwner != i->first))
		{
			i->second->objectRemoved(o, pack.initiator);
			if (h && h->inBoat())
				i->second->objectRemoved(h->getBoat(), pack.initiator);
		}
	}

	GAME->map().waitForOngoingAnimations();
}

void ApplyClientNetPackVisitor::visitRemoveObject(RemoveObject & pack)
{
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);

	for(auto i=cl.playerint.begin(); i!=cl.playerint.end(); i++)
		i->second->objectRemovedAfter();
}

void ApplyFirstClientNetPackVisitor::visitTryMoveHero(TryMoveHero & pack)
{
	const CGHeroInstance *h = gs.getHero(pack.id);

	switch (pack.result)
	{
		case TryMoveHero::EMBARK:
			GAME->map().onBeforeHeroEmbark(h, pack.start, pack.end);
			GAME->map().waitForOngoingAnimations(); // required - hero must play fade-out animation on his pre-embark position
			break;
		case TryMoveHero::TELEPORTATION:
			GAME->map().onBeforeHeroTeleported(h, pack.start, pack.end);
			break;
		case TryMoveHero::DISEMBARK:
			GAME->map().onBeforeHeroDisembark(h, pack.start, pack.end);
			break;
	}
}

void ApplyClientNetPackVisitor::visitTryMoveHero(TryMoveHero & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.id);
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);

	switch(pack.result)
	{
		case TryMoveHero::SUCCESS:
			GAME->map().onHeroMoved(h, pack.start, pack.end);
			break;
		case TryMoveHero::EMBARK:
			GAME->map().onAfterHeroEmbark(h, pack.start, pack.end);
			break;
		case TryMoveHero::TELEPORTATION:
			GAME->map().onAfterHeroTeleported(h, pack.start, pack.end);
			break;
		case TryMoveHero::DISEMBARK:
			GAME->map().onAfterHeroDisembark(h, pack.start, pack.end);
			break;
	}

	PlayerColor player = h->tempOwner;

	for(auto &i : cl.playerint)
		if(cl.gameInfo().getPlayerRelations(i.first, player) != PlayerRelations::ENEMIES)
			i.second->tileRevealed(pack.fowRevealed);

	for(auto i=cl.playerint.begin(); i!=cl.playerint.end(); i++)
	{
		if(i->first != PlayerColor::SPECTATOR && gs.checkForStandardLoss(i->first)) // Do not notify vanquished pack.player's interface
			continue;

		if(gs.isVisibleFor(h->convertToVisitablePos(pack.start), i->first)
			|| gs.isVisibleFor(h->convertToVisitablePos(pack.end), i->first))
		{
			// pack.src and pack.dst of enemy hero move may be not visible => 'verbose' should be false
			const bool verbose = cl.gameInfo().getPlayerRelations(i->first, player) != PlayerRelations::ENEMIES;
			i->second->heroMoved(pack, verbose);
		}
	}

	GAME->map().waitForOngoingAnimations();
}

void ApplyClientNetPackVisitor::visitNewStructures(NewStructures & pack)
{
	const CGTownInstance *town = gs.getTown(pack.tid);
	for(const auto & id : pack.bid)
	{
		callInterfaceIfPresent(cl, town->getOwner(), &IGameEventsReceiver::buildChanged, town, id, 1);
	}

	if(settings["session"]["headless"].Bool())
		return;

	// invalidate section of map view with our object and force an update
	GAME->map().onObjectInstantRemove(town, town->getOwner());
	GAME->map().onObjectInstantAdd(town, town->getOwner());
}

void ApplyClientNetPackVisitor::visitRazeStructures(RazeStructures & pack)
{
	const CGTownInstance * town = gs.getTown(pack.tid);
	for(const auto & id : pack.bid)
	{
		callInterfaceIfPresent(cl, town->getOwner(), &IGameEventsReceiver::buildChanged, town, id, 2);
	}

	if(settings["session"]["headless"].Bool())
		return;

	// invalidate section of map view with our object and force an update
	GAME->map().onObjectInstantRemove(town, town->getOwner());
	GAME->map().onObjectInstantAdd(town, town->getOwner());
}

void ApplyClientNetPackVisitor::visitSetAvailableCreatures(SetAvailableCreatures & pack)
{
	const CGDwelling * dw = static_cast<const CGDwelling*>(cl.gameInfo().getObj(pack.tid));

	PlayerColor p;
	if(dw->ID == Obj::WAR_MACHINE_FACTORY) //War Machines Factory is not flaggable, it's "owned" by visitor
		p = cl.gameInfo().getObjInstance(cl.gameInfo().getTile(dw->visitablePos())->visitableObjects.back())->getOwner();
	else
		p = dw->tempOwner;

	callInterfaceIfPresent(cl, p, &IGameEventsReceiver::availableCreaturesChanged, dw);
}

void ApplyClientNetPackVisitor::visitSetHeroesInTown(SetHeroesInTown & pack)
{
	const CGTownInstance * t = gs.getTown(pack.tid);
	const CGHeroInstance * hGarr  = gs.getHero(pack.garrison);
	const CGHeroInstance * hVisit = gs.getHero(pack.visiting);

	//inform all players that see this object
	for(auto i = cl.playerint.cbegin(); i != cl.playerint.cend(); ++i)
	{
		if(!i->first.isValidPlayer())
			continue;

		if(gs.isVisibleFor(t, i->first) ||
			(hGarr && gs.isVisibleFor(hGarr, i->first)) ||
			(hVisit && gs.isVisibleFor(hVisit, i->first)))
		{
			cl.playerint[i->first]->heroInGarrisonChange(t);
		}
	}
}

void ApplyClientNetPackVisitor::visitHeroRecruited(HeroRecruited & pack)
{
	const auto * h = gs.getMap().getHero(pack.hid);
	if(h->getHeroTypeID() != pack.hid)
	{
		logNetwork->error("Something wrong with hero recruited!");
	}

	if(callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroCreated, h))
	{
		if(const CGTownInstance *t = gs.getTown(pack.tid))
			callInterfaceIfPresent(cl, h->getOwner(), &IGameEventsReceiver::heroInGarrisonChange, t);
	}
	GAME->map().onObjectInstantAdd(h, h->getOwner());
}

void ApplyClientNetPackVisitor::visitGiveHero(GiveHero & pack)
{
	const CGHeroInstance *h = gs.getHero(pack.id);
	GAME->map().onObjectInstantAdd(h, h->getOwner());
	callInterfaceIfPresent(cl, h->tempOwner, &IGameEventsReceiver::heroCreated, h);
}

void ApplyFirstClientNetPackVisitor::visitGiveHero(GiveHero & pack)
{
}

void ApplyClientNetPackVisitor::visitInfoWindow(InfoWindow & pack)
{
	std::string str = pack.text.toString();

	if(!callInterfaceIfPresent(cl, pack.player, &CGameInterface::showInfoDialog, pack.type, str, pack.components,(soundBase::soundID)pack.soundID))
		logNetwork->warn("We received InfoWindow for not our player...");
}

void ApplyFirstClientNetPackVisitor::visitSetObjectProperty(SetObjectProperty & pack)
{
	//inform all players that see this object
	for(auto it = cl.playerint.cbegin(); it != cl.playerint.cend(); ++it)
	{
		if(gs.isVisibleFor(gs.getObjInstance(pack.id), it->first))
			callInterfaceIfPresent(cl, it->first, &IGameEventsReceiver::beforeObjectPropertyChanged, &pack);
	}

	// invalidate section of map view with our object and force an update with new flag color
	if(pack.what == ObjProperty::OWNER)
	{
		auto object = gs.getObjInstance(pack.id);
		GAME->map().onObjectInstantRemove(object, object->getOwner());
	}
}

void ApplyClientNetPackVisitor::visitSetObjectProperty(SetObjectProperty & pack)
{
	//inform all players that see this object
	for(auto it = cl.playerint.cbegin(); it != cl.playerint.cend(); ++it)
	{
		if(gs.isVisibleFor(gs.getObjInstance(pack.id), it->first))
			callInterfaceIfPresent(cl, it->first, &IGameEventsReceiver::objectPropertyChanged, &pack);
	}

	// invalidate section of map view with our object and force an update with new flag color
	if(pack.what == ObjProperty::OWNER)
	{
		auto object = gs.getObjInstance(pack.id);
		GAME->map().onObjectInstantAdd(object, object->getOwner());
	}
}

void ApplyClientNetPackVisitor::visitHeroLevelUp(HeroLevelUp & pack)
{
	const CGHeroInstance * hero = cl.gameInfo().getHero(pack.heroId);
	assert(hero);
	callOnlyThatInterface(cl, pack.player, &CGameInterface::heroGotLevel, hero, pack.primskill, pack.skills, pack.queryID);
}

void ApplyClientNetPackVisitor::visitCommanderLevelUp(CommanderLevelUp & pack)
{
	const CGHeroInstance * hero = cl.gameInfo().getHero(pack.heroId);
	assert(hero);
	const auto & commander = hero->getCommander();
	assert(commander);
	assert(commander->getArmy()); //is it possible for Commander to exist beyond armed instance?
	callOnlyThatInterface(cl, pack.player, &CGameInterface::commanderGotLevel, commander, pack.skills, pack.queryID);
}

void ApplyClientNetPackVisitor::visitBlockingDialog(BlockingDialog & pack)
{
	std::string str = pack.text.toString();

	if(!callOnlyThatInterface(cl, pack.player, &CGameInterface::showBlockingDialog, str, pack.components, pack.queryID, (soundBase::soundID)pack.soundID, pack.selection(), pack.cancel(), pack.safeToAutoaccept()))
		logNetwork->warn("We received YesNoDialog for not our player...");
}

void ApplyClientNetPackVisitor::visitGarrisonDialog(GarrisonDialog & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.hid);
	const CArmedInstance *obj = static_cast<const CArmedInstance*>(cl.gameInfo().getObj(pack.objid));

	callOnlyThatInterface(cl, h->getOwner(), &CGameInterface::showGarrisonDialog, obj, h, pack.removableUnits, pack.queryID, pack.customTitle);
}

void ApplyClientNetPackVisitor::visitExchangeDialog(ExchangeDialog & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::heroExchangeStarted, pack.hero1, pack.hero2, pack.queryID);
}

void ApplyClientNetPackVisitor::visitTeleportDialog(TeleportDialog & pack)
{
	const CGHeroInstance *h = cl.gameInfo().getHero(pack.hero);
	callOnlyThatInterface(cl, h->getOwner(), &CGameInterface::showTeleportDialog, h, pack.channel, pack.exits, pack.impassable, pack.queryID);
}

void ApplyClientNetPackVisitor::visitMapObjectSelectDialog(MapObjectSelectDialog & pack)
{
	callOnlyThatInterface(cl, pack.player, &CGameInterface::showMapObjectSelectDialog, pack.queryID, pack.icon, pack.title, pack.description, pack.objects);
}

void ApplyFirstClientNetPackVisitor::visitBattleStart(BattleStart & pack)
{
	// Cannot use the usual code because curB is not set yet
	callOnlyThatBattleInterface(cl, pack.info->getSide(BattleSide::ATTACKER).color, &IBattleEventsReceiver::battleStartBefore, pack.battleID, pack.info->getSideArmy(BattleSide::ATTACKER), pack.info->getSideArmy(BattleSide::DEFENDER),
		pack.info->tile, pack.info->getSideHero(BattleSide::ATTACKER), pack.info->getSideHero(BattleSide::DEFENDER));
	callOnlyThatBattleInterface(cl, pack.info->getSide(BattleSide::DEFENDER).color, &IBattleEventsReceiver::battleStartBefore, pack.battleID, pack.info->getSideArmy(BattleSide::ATTACKER), pack.info->getSideArmy(BattleSide::DEFENDER),
		pack.info->tile, pack.info->getSideHero(BattleSide::ATTACKER), pack.info->getSideHero(BattleSide::DEFENDER));
	callOnlyThatBattleInterface(cl, PlayerColor::SPECTATOR, &IBattleEventsReceiver::battleStartBefore, pack.battleID, pack.info->getSideArmy(BattleSide::ATTACKER), pack.info->getSideArmy(BattleSide::DEFENDER),
		pack.info->tile, pack.info->getSideHero(BattleSide::ATTACKER), pack.info->getSideHero(BattleSide::DEFENDER));
}

void ApplyClientNetPackVisitor::visitBattleStart(BattleStart & pack)
{
	if(GAME->interface())
	{
		GAME->interface()->setArenaDeploymentActive(false);
		const auto player = GAME->interface()->playerID;
		ENGINE->dispatchMainThread([player]()
		{
			if(auto * current = GAME->interface(); current && current->playerID == player && adventureInt)
				adventureInt->refreshArenaNextBattleTerrainLabel();
		});
	}
	cl.battleStarted(pack.battleID);
}

void ApplyFirstClientNetPackVisitor::visitBattleNextRound(BattleNextRound & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleNewRoundFirst, pack.battleID);
}

void ApplyClientNetPackVisitor::visitBattleNextRound(BattleNextRound & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleNewRound, pack.battleID);
}

void ApplyClientNetPackVisitor::visitBattleSetActiveStack(BattleSetActiveStack & pack)
{
	if(pack.reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
		return;

	const CStack *activated = gs.getBattle(pack.battleID)->battleGetStackByID(pack.stack);
	PlayerColor playerToCall; //pack.player that will move activated stack
	if(activated->isHypnotized())
	{
		playerToCall = gs.getBattle(pack.battleID)->getSide(BattleSide::ATTACKER).color == activated->unitOwner()
			? gs.getBattle(pack.battleID)->getSide(BattleSide::DEFENDER).color
			: gs.getBattle(pack.battleID)->getSide(BattleSide::ATTACKER).color;
	}
	else
	{
		playerToCall = activated->unitOwner();
	}

	cl.startPlayerBattleAction(pack.battleID, playerToCall);
}

void ApplyClientNetPackVisitor::visitBattleLogMessage(BattleLogMessage & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleLogMessage, pack.battleID, pack.lines);
}

void ApplyClientNetPackVisitor::visitBattleTriggerEffect(BattleTriggerEffect & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleTriggerEffect, pack.battleID, pack);
}

void ApplyFirstClientNetPackVisitor::visitBattleUpdateGateState(BattleUpdateGateState & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleGateStateChanged, pack.battleID, pack.state);
}

void ApplyFirstClientNetPackVisitor::visitBattleResult(BattleResult & pack)
{
	if(gs.getMapHeader()->battleOnly)
	{
		const BattleResult result = pack;
		const auto * battle = gs.getBattle(result.battleID);
		if(!battle)
			logGlobal->error("[ARENA] battle result observation missing battle=%d", result.battleID.getNum());
		else
		{
			auto & client = cl;
			auto notifyDeploymentPlanner = [&client, &result](PlayerColor color, BattleSide side, const std::map<CreatureID, si32> & enemyCasualties)
			{
				callInterfaceIfPresent(client, color, &IGameEventsReceiver::arenaBattleResultObservation, result.winner == side, enemyCasualties);
			};
			notifyDeploymentPlanner(battle->getSide(BattleSide::LEFT_SIDE).color, BattleSide::LEFT_SIDE, result.casualties[BattleSide::RIGHT_SIDE]);
			notifyDeploymentPlanner(battle->getSide(BattleSide::RIGHT_SIDE).color, BattleSide::RIGHT_SIDE, result.casualties[BattleSide::LEFT_SIDE]);

			auto notifyRecruitmentThresholds = [&client, &result, battle](PlayerColor color, BattleSide enemySide)
			{
				std::map<CreatureID, EArenaRecruitmentThresholdState> merged;
				const PlayerColor enemyColor = battle->getSide(enemySide).color;
				for(const auto & observation : client.getArenaBattleRecruitmentThresholds(result.battleID, enemyColor))
					merged[observation.creature] = observation.state;
				if(const auto * enemyArmy = battle->getSide(enemySide).getArmy())
					for(const auto & [slot, stack] : enemyArmy->stacks)
						if(stack && stack->getCreatureID() != CreatureID::NONE)
							merged.try_emplace(stack->getCreatureID(), EArenaRecruitmentThresholdState::UNKNOWN);

				std::vector<ArenaRecruitmentThresholdObservation> observations;
				observations.reserve(merged.size());
				for(const auto & [creature, state] : merged)
					observations.push_back({creature, state});
				callInterfaceIfPresent(client, color, &IGameEventsReceiver::arenaBattleRecruitmentThresholdObservation, observations);
			};
			notifyRecruitmentThresholds(battle->getSide(BattleSide::LEFT_SIDE).color, BattleSide::RIGHT_SIDE);
			notifyRecruitmentThresholds(battle->getSide(BattleSide::RIGHT_SIDE).color, BattleSide::LEFT_SIDE);
		}
		if(!ENGINE)
		{
			callBattleInterfaceIfPresentForBothSides(cl, result.battleID, &IBattleEventsReceiver::battleEnd, result.battleID, &result, result.queryID);
			cl.battleFinished(result.battleID);
			return;
		}
		ENGINE->dispatchMainThread([client = &cl, result]() mutable
		{
			logGlobal->info("[ARENA] displaying asynchronous battle result: battle=%d query=%d", result.battleID.getNum(), result.queryID.getNum());
			callBattleInterfaceIfPresentForBothSides(*client, result.battleID, &IBattleEventsReceiver::battleEnd, result.battleID, &result, result.queryID);
			client->battleFinished(result.battleID);
		});
		return;
	}

	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleEnd, pack.battleID, &pack, pack.queryID);
	cl.battleFinished(pack.battleID);
}

void ApplyFirstClientNetPackVisitor::visitBattleStackMoved(BattleStackMoved & pack)
{
	const CStack * movedStack = gs.getBattle(pack.battleID)->battleGetStackByID(pack.stack);
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleStackMoved, pack.battleID, movedStack, pack.tilesToMove, pack.distance, pack.teleporting);
}

void ApplyFirstClientNetPackVisitor::visitBattleAttack(BattleAttack & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleAttack, pack.battleID, &pack);

	// battleStacksAttacked should be executed before BattleAttack.applyGs() to play animation before damaging unit
	// so this has to be here instead of ApplyClientNetPackVisitor::visitBattleAttack()
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleStacksAttacked, pack.battleID, pack.bsa, pack.shot());
}

void ApplyClientNetPackVisitor::visitBattleAttack(BattleAttack & pack)
{
}

void ApplyFirstClientNetPackVisitor::visitStartAction(StartAction & pack)
{
	cl.currentBattleAction = std::make_unique<BattleAction>(pack.ba);
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::actionStarted, pack.battleID, pack.ba);
}

void ApplyClientNetPackVisitor::visitBattleSpellCast(BattleSpellCast & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleSpellCast, pack.battleID, &pack);
}

void ApplyClientNetPackVisitor::visitSetStackEffect(SetStackEffect & pack)
{
	//informing about effects
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleStacksEffectsSet, pack.battleID, pack);
}

void ApplyClientNetPackVisitor::visitStacksInjured(StacksInjured & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleStacksAttacked, pack.battleID, pack.stacks, false);
}

void ApplyClientNetPackVisitor::visitBattleResultsApplied(BattleResultsApplied & pack)
{
	if(!pack.learnedSpells.spells.empty())
	{
		const auto * hero = cl.gameInfo().getHero(pack.learnedSpells.hid);
		assert(hero);
		callInterfaceIfPresent(cl, pack.victor, &CGameInterface::showInfoDialog, EInfoWindowMode::MODAL,
			UIHelper::getEagleEyeInfoWindowText(*hero, pack.learnedSpells.spells), UIHelper::getSpellsComponents(pack.learnedSpells.spells), soundBase::soundID(0));
	}

	if(!pack.movingArtifacts.empty())
	{
		const auto * artSet = cl.gameState().getArtSet(ArtifactLocation(pack.movingArtifacts.front().dstArtHolder));
		assert(artSet);
		std::vector<Component> artComponents;
		for(const auto & artPack : pack.movingArtifacts)
		{
			auto packComponents = UIHelper::getArtifactsComponents(*artSet, artPack.artsPack0);
			artComponents.insert(artComponents.end(), std::make_move_iterator(packComponents.begin()), std::make_move_iterator(packComponents.end()));
		}
		callInterfaceIfPresent(cl, pack.victor, &CGameInterface::showInfoDialog, EInfoWindowMode::MODAL, UIHelper::getArtifactsInfoWindowText(),
			artComponents, soundBase::soundID(0));
	}

	for(auto & artPack : pack.movingArtifacts)
		visitBulkMoveArtifacts(artPack);

	if(pack.raisedStack.getCreature())
		callInterfaceIfPresent(cl, pack.victor, &CGameInterface::showInfoDialog, EInfoWindowMode::AUTO,
			UIHelper::getNecromancyInfoWindowText(pack.raisedStack), std::vector<Component>{Component(ComponentType::CREATURE, pack.raisedStack.getId(),
			pack.raisedStack.getCount())}, UIHelper::getNecromancyInfoWindowSound());

	callInterfaceIfPresent(cl, pack.victor, &IGameEventsReceiver::battleResultsApplied);
	callInterfaceIfPresent(cl, pack.loser, &IGameEventsReceiver::battleResultsApplied);
	callInterfaceIfPresent(cl, PlayerColor::SPECTATOR, &IGameEventsReceiver::battleResultsApplied);
}

void ApplyClientNetPackVisitor::visitBattleEnded(BattleEnded & pack)
{
	cl.clearArenaBattleRecruitmentThresholds(pack.battleID);
	if(GAME->interface())
		GAME->interface()->clearArenaBattleCreatureProgress(pack.battleID);
	callInterfaceIfPresent(cl, pack.victor, &IGameEventsReceiver::battleEnded);
	callInterfaceIfPresent(cl, pack.loser, &IGameEventsReceiver::battleEnded);
	callInterfaceIfPresent(cl, PlayerColor::SPECTATOR, &IGameEventsReceiver::battleEnded);

	if(gs.getMapHeader()->battleOnly)
	{
		const BattleID battleID = pack.battleID;
		if(settings["session"]["headless"].Bool())
		{
			if(CPlayerInterface::battleInt && CPlayerInterface::battleInt->getBattleID() == battleID)
			{
				logGlobal->info("[ARENA] releasing headless battle interface after BattleEnded: battle=%d", battleID.getNum());
				CPlayerInterface::battleInt.reset();
			}
			return;
		}
		if(!ENGINE)
			return;
		ENGINE->dispatchMainThread([battleID]()
		{
			if(CPlayerInterface::battleInt && CPlayerInterface::battleInt->getBattleID() == battleID)
			{
				logGlobal->info("[ARENA] releasing battle interface after BattleEnded: battle=%d", battleID.getNum());
				CPlayerInterface::battleInt.reset();
			}
		});
	}
}

void ApplyClientNetPackVisitor::visitBattleUnitsChanged(BattleUnitsChanged & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleUnitsChanged, pack.battleID, pack.changedStacks);
}

void ApplyClientNetPackVisitor::visitBattleObstaclesChanged(BattleObstaclesChanged & pack)
{
	//inform interfaces about removed obstacles
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleObstaclesChanged, pack.battleID, pack.changes);
}

void ApplyClientNetPackVisitor::visitCatapultAttack(CatapultAttack & pack)
{
	//inform interfaces about catapult attack
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleCatapultAttacked, pack.battleID, pack);
}

void ApplyClientNetPackVisitor::visitEndAction(EndAction & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::actionFinished, pack.battleID, *cl.currentBattleAction);
	cl.currentBattleAction.reset();
}

void ApplyClientNetPackVisitor::visitPackageApplied(PackageApplied & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::requestRealized, &pack);
	if(!cl.waitingRequest.tryRemovingElement(pack.requestID))
		logNetwork->warn("Surprising server message! PackageApplied for unknown requestID!");
}

void ApplyClientNetPackVisitor::visitSystemMessage(SystemMessage & pack)
{
	const auto text = pack.text.toString();
	// Arena developer-command receipts deliberately use SystemMessage so they
	// stay visible in the in-game system chat. They are expected command
	// feedback, including a rejected request, not client or server errors.
	if(boost::starts_with(text, "[ARENA] "))
		logNetwork->info("System message: %s", text);
	else
		logNetwork->error("System message: %s", text);

	// Network packs are applied on the network thread, while the chat console is
	// a GUI object. Updating it directly can deadlock with battle-result window
	// transitions, notably when an arena debug receipt follows BattleResult.
	if(ENGINE)
	ENGINE->dispatchMainThread([text]()
	{
		GAME->server().getGameChat().onNewSystemMessageReceived(text);
	});
}

void ApplyClientNetPackVisitor::visitPlayerBlocked(PlayerBlocked & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::playerBlocked, pack.reason, pack.startOrEnd == PlayerBlocked::BLOCKADE_STARTED);
}

void ApplyClientNetPackVisitor::visitPlayerStartsTurn(PlayerStartsTurn & pack)
{
	logNetwork->debug("Server gives turn to %s", pack.player.toString());

	callAllInterfaces(cl, &IGameEventsReceiver::playerStartsTurn, pack.player);
	callOnlyThatInterface(cl, pack.player, &CGameInterface::yourTurn, pack.queryID);
}

void ApplyClientNetPackVisitor::visitPlayerEndsTurn(PlayerEndsTurn & pack)
{
	logNetwork->debug("Server ends turn of %s", pack.player.toString());

	callAllInterfaces(cl, &IGameEventsReceiver::playerEndsTurn, pack.player);
}

void ApplyClientNetPackVisitor::visitTurnTimeUpdate(TurnTimeUpdate & pack)
{
	logNetwork->debug("Server sets turn timer {turn: %d, base: %d, battle: %d, creature: %d} for %s", pack.turnTimer.turnTimer, pack.turnTimer.baseTimer, pack.turnTimer.battleTimer, pack.turnTimer.unitTimer, pack.player.toString());
	if(!pack.player.isValidPlayer() || !pack.turnTimer.isActive || pack.turnTimer.isBattle || pack.turnTimer.turnTimer <= 0)
		return;
	if(GAME->interface()
		&& GAME->interface()->playerID == pack.player
		&& GAME->interface()->arenaDeploymentActive.load())
		logGlobal->debug("[ARENA-MONITOR] deployment_timer_authority player=%s remaining_ms=%d", pack.player.toString(), pack.turnTimer.turnTimer);
	const auto timerPlayer = pack.player;
	if(!ENGINE)
		return;
	ENGINE->dispatchMainThread([timerPlayer]()
	{
		auto * current = GAME->interface();
		if(!current || !adventureInt || !timerPlayer.isValidPlayer() || !current->cb->getMapHeader()->battleOnly)
			return;
		adventureInt->ensureArenaDeploymentTimer();
	});
}

void ApplyClientNetPackVisitor::visitPlayerMessageClient(PlayerMessageClient & pack)
{
	logNetwork->debug("pack.player %s sends a message: %s", pack.player.toString(), pack.text);

	GAME->server().getGameChat().onNewGameMessageReceived(pack.player, pack.text);
}

void ApplyClientNetPackVisitor::visitAdvmapSpellCast(AdvmapSpellCast & pack)
{
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);

	auto caster = cl.gameInfo().getHero(pack.casterID);
	if(caster)
		//consider notifying other interfaces that see hero?
		callInterfaceIfPresent(cl, caster->getOwner(), &IGameEventsReceiver::advmapSpellCast, caster, pack.spellID);
	else
		logNetwork->error("Invalid hero instance");
}

void ApplyClientNetPackVisitor::visitShowWorldViewEx(ShowWorldViewEx & pack)
{
	callOnlyThatInterface(cl, pack.player, &CGameInterface::showWorldViewEx, pack.objectPositions, pack.showTerrain);
}

void ApplyClientNetPackVisitor::visitOpenWindow(OpenWindow & pack)
{
	switch(pack.window)
	{
	case EOpenWindowMode::RECRUITMENT_FIRST:
	case EOpenWindowMode::RECRUITMENT_ALL:
		{
			const CGDwelling *dw = dynamic_cast<const CGDwelling*>(cl.gameInfo().getObj(ObjectInstanceID(pack.object)));
			const CArmedInstance *dst = dynamic_cast<const CArmedInstance*>(cl.gameInfo().getObj(ObjectInstanceID(pack.visitor)));
			callInterfaceIfPresent(cl, dst->tempOwner, &IGameEventsReceiver::showRecruitmentDialog, dw, dst, pack.window == EOpenWindowMode::RECRUITMENT_FIRST ? 0 : -1, pack.queryID);
		}
		break;
	case EOpenWindowMode::SHIPYARD_WINDOW:
		{
			assert(pack.queryID == QueryID::NONE);
			const auto * sy = dynamic_cast<const IShipyard *>(cl.gameInfo().getObj(ObjectInstanceID(pack.object)));
			callInterfaceIfPresent(cl, sy->getObject()->getOwner(), &IGameEventsReceiver::showShipyardDialog, sy);
		}
		break;
	case EOpenWindowMode::THIEVES_GUILD:
		{
			assert(pack.queryID == QueryID::NONE);
			//displays Thieves' Guild window (when hero enters Den of Thieves)
			const CGObjectInstance *obj = cl.gameInfo().getObj(ObjectInstanceID(pack.object));
			const CGHeroInstance *hero = cl.gameInfo().getHero(ObjectInstanceID(pack.visitor));
			callInterfaceIfPresent(cl, hero->getOwner(), &IGameEventsReceiver::showThievesGuildWindow, obj);
		}
		break;
	case EOpenWindowMode::UNIVERSITY_WINDOW:
		{
			//displays University window (when hero enters University on adventure map)
			const auto * market = cl.gameState().getMarket(ObjectInstanceID(pack.object));
			const CGHeroInstance *hero = cl.gameInfo().getHero(ObjectInstanceID(pack.visitor));
			callInterfaceIfPresent(cl, hero->tempOwner, &IGameEventsReceiver::showUniversityWindow, market, hero, pack.queryID);
		}
		break;
	case EOpenWindowMode::MARKET_WINDOW:
		{
			//displays Thieves' Guild window (when hero enters Den of Thieves)
			const CGObjectInstance *obj = cl.gameInfo().getObj(ObjectInstanceID(pack.object));
			const CGHeroInstance *hero = cl.gameInfo().getHero(ObjectInstanceID(pack.visitor));
			const auto market = cl.gameState().getMarket(pack.object);
			const auto * tile = cl.gameInfo().getTile(obj->visitablePos());
			const auto * topObject = cl.gameInfo().getObjInstance(tile->visitableObjects.back());
			callInterfaceIfPresent(cl, topObject->getOwner(), &IGameEventsReceiver::showMarketWindow, market, hero, pack.queryID);
		}
		break;
	case EOpenWindowMode::HILL_FORT_WINDOW:
		{
			assert(pack.queryID == QueryID::NONE);
			//displays Hill fort window
			const CGObjectInstance *obj = cl.gameInfo().getObj(ObjectInstanceID(pack.object));
			const CGHeroInstance *hero = cl.gameInfo().getHero(ObjectInstanceID(pack.visitor));
			const auto * tile = cl.gameInfo().getTile(obj->visitablePos());
			const auto * topObject = cl.gameInfo().getObjInstance(tile->visitableObjects.back());
			callInterfaceIfPresent(cl, topObject->getOwner(), &IGameEventsReceiver::showHillFortWindow, obj, hero);
		}
		break;
	case EOpenWindowMode::PUZZLE_MAP:
		{
			assert(pack.queryID == QueryID::NONE);
			const CGHeroInstance *hero = cl.gameInfo().getHero(ObjectInstanceID(pack.visitor));
			callInterfaceIfPresent(cl, hero->getOwner(), &IGameEventsReceiver::showPuzzleMap);
		}
		break;
	case EOpenWindowMode::TAVERN_WINDOW:
		{
			const CGObjectInstance *obj1 = cl.gameInfo().getObj(ObjectInstanceID(pack.object));
			const CGHeroInstance * hero = cl.gameInfo().getHero(ObjectInstanceID(pack.visitor));
			callInterfaceIfPresent(cl, hero->tempOwner, &IGameEventsReceiver::showTavernWindow, obj1, hero, pack.queryID);
		}
		break;
	}
}

void ApplyClientNetPackVisitor::visitCenterView(CenterView & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::centerView, pack.pos, pack.focusTime);
}

void ApplyClientNetPackVisitor::visitNewObject(NewObject & pack)
{
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);

	const CGObjectInstance * obj = pack.newObject.get();
	if(!settings["session"]["headless"].Bool())
		GAME->map().onObjectFadeIn(obj, pack.initiator);

	for(auto i=cl.playerint.begin(); i!=cl.playerint.end(); i++)
	{
		if(gs.isVisibleFor(obj, i->first))
			i->second->newObject(obj);
	}

	if(!settings["session"]["headless"].Bool())
		GAME->map().waitForOngoingAnimations();
}

void ApplyClientNetPackVisitor::visitSetAvailableArtifacts(SetAvailableArtifacts & pack)
{
	if(!pack.id.hasValue()) //artifact merchants globally
	{
		callAllInterfaces(cl, &IGameEventsReceiver::availableArtifactsChanged, nullptr);
	}
	else
	{
		const CGBlackMarket *bm = dynamic_cast<const CGBlackMarket *>(cl.gameInfo().getObj(ObjectInstanceID(pack.id)));
		assert(bm);
		const auto * tile = cl.gameInfo().getTile(bm->visitablePos());
		const auto * topObject = cl.gameInfo().getObjInstance(tile->visitableObjects.back());

		callInterfaceIfPresent(cl, topObject->getOwner(), &IGameEventsReceiver::availableArtifactsChanged, bm);
	}
}

void ApplyClientNetPackVisitor::visitEntitiesChanged(EntitiesChanged & pack)
{
	callAllInterfaces(cl, &CGameInterface::invalidatePaths);
}

void ApplyClientNetPackVisitor::visitPlayerCheated(PlayerCheated & pack)
{
	if(pack.colorScheme != ColorScheme::KEEP && vstd::contains(cl.playerint, pack.player))
		cl.playerint[pack.player]->setColorScheme(pack.colorScheme);
}

void ApplyClientNetPackVisitor::visitChangeTownName(ChangeTownName & pack)
{
	if(!adventureInt)
		return;

	const CGTownInstance *town = gs.getTown(pack.tid);
	if(town)
	{
		adventureInt->onTownChanged(town);
		ENGINE->windows().totalRedraw();
	}
}

void ApplyClientNetPackVisitor::visitResponseStatistic(ResponseStatistic & pack)
{
	callInterfaceIfPresent(cl, pack.player, &IGameEventsReceiver::responseStatistic, pack.statistic);
}
