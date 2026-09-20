/*
 * Client.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Global.h"
#include "Client.h"
#include "../lib/IGameSettings.h"

#include "CPlayerInterface.h"
#include "PlayerLocalState.h"
#include "CServerHandler.h"
#include "ClientNetPackVisitors.h"
#include "adventureMap/AdventureMapInterface.h"
#include "battle/BattleInterface.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "gui/WindowHandler.h"
#include "mainmenu/CMainMenu.h"
#include "mapView/mapHandler.h"

#include "../lib/CConfigHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/battle/AutocombatPreferences.h"
#include "../lib/battle/CPlayerBattleCallback.h"
#include "../lib/callback/CCallback.h"
#include "../lib/callback/CDynLibHandler.h"
#include "../lib/callback/CGlobalAI.h"
#include "../lib/callback/IGameInfoCallback.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/CPlayerState.h"
#include "../lib/CThreadHelper.h"
#include "../lib/VCMIDirs.h"
#include "../lib/UnlockGuard.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/army/CArmedInstance.h"
#include "../lib/mapping/CMap.h"
#include "../lib/mapping/CMapService.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/pathfinder/CGPathNode.h"
#include "../lib/serializer/GameConnection.h"

#include <memory>
#include <vcmi/events/EventBus.h>

#if SCRIPTING_ENABLED
#include "../lib/ScriptHandler.h"
#endif

#ifdef VCMI_ANDROID
#include "lib/CAndroidVMHelper.h"
#endif

CPlayerEnvironment::CPlayerEnvironment(PlayerColor player_, CClient * cl_, std::shared_ptr<CCallback> mainCallback_)
	: player(player_),
	cl(cl_),
	mainCallback(mainCallback_)
{

}

const Services * CPlayerEnvironment::services() const
{
	return LIBRARY;
}

vstd::CLoggerBase * CPlayerEnvironment::logger() const
{
	return logGlobal;
}

events::EventBus * CPlayerEnvironment::eventBus() const
{
	return cl->eventBus();//always get actual value
}

const CPlayerEnvironment::BattleCb * CPlayerEnvironment::battle(const BattleID & battleID) const
{
	return mainCallback->getBattle(battleID).get();
}

const CPlayerEnvironment::GameCb * CPlayerEnvironment::game() const
{
	return mainCallback.get();
}

CClient::CClient()
{
	waitingRequest.clear();
}

CClient::~CClient() = default;

void CClient::updateArenaBattleRecruitmentThresholds(const SetArenaBattleCreatureProgress & pack)
{
	std::map<PlayerColor, std::map<CreatureID, EArenaRecruitmentThresholdState>> sanitized;
	for(const auto & entry : pack.entries)
	{
		if(!entry.player.isValidPlayer() || entry.creature == CreatureID::NONE)
			continue;

		auto state = EArenaRecruitmentThresholdState::UNKNOWN;
		if(entry.currentExperience >= 0 && entry.maximumExperience > 0)
			state = entry.currentExperience >= entry.maximumExperience
				? EArenaRecruitmentThresholdState::READY
				: EArenaRecruitmentThresholdState::BELOW;
		sanitized[entry.player][entry.creature] = state;
	}

	std::lock_guard lock(arenaBattleRecruitmentThresholdMutex);
	arenaBattleRecruitmentThresholdCache[pack.battleID] = std::move(sanitized);
}

std::vector<ArenaRecruitmentThresholdObservation> CClient::getArenaBattleRecruitmentThresholds(BattleID battleID, PlayerColor observedPlayer) const
{
	std::vector<ArenaRecruitmentThresholdObservation> result;
	std::lock_guard lock(arenaBattleRecruitmentThresholdMutex);
	const auto battle = arenaBattleRecruitmentThresholdCache.find(battleID);
	if(battle == arenaBattleRecruitmentThresholdCache.end())
		return result;
	const auto player = battle->second.find(observedPlayer);
	if(player == battle->second.end())
		return result;
	result.reserve(player->second.size());
	for(const auto & [creature, state] : player->second)
		result.push_back({creature, state});
	return result;
}

void CClient::clearArenaBattleRecruitmentThresholds(BattleID battleID)
{
	std::lock_guard lock(arenaBattleRecruitmentThresholdMutex);
	arenaBattleRecruitmentThresholdCache.erase(battleID);
}

IGameInfoCallback & CClient::gameInfo()
{
	return *gamestate;
}

const Services * CClient::services() const
{
	return LIBRARY; //todo: this should be LIBRARY
}

const CClient::BattleCb * CClient::battle(const BattleID & battleID) const
{
	return nullptr; //todo?
}

const CClient::GameCb * CClient::game() const
{
	return gamestate.get();
}

vstd::CLoggerBase * CClient::logger() const
{
	return logGlobal;
}

events::EventBus * CClient::eventBus() const
{
	return clientEventBus.get();
}

void CClient::newGame(std::shared_ptr<CGameState> initializedGameState)
{
	GAME->server().th->update();
	assert(initializedGameState);
	{
		std::lock_guard lock(arenaHealthMutex);
		arenaHealthCache.clear();
	}
	gamestate = initializedGameState;
	gamestate->preInit(LIBRARY);
	logNetwork->trace("\tCreating gamestate: %i", GAME->server().th->getDiff());

	initMapHandler();
	reinitScripting();
	initPlayerEnvironments();
	initPlayerInterfaces();
}

void CClient::loadGame(std::shared_ptr<CGameState> initializedGameState)
{
	logNetwork->info("Loading procedure started!");
	{
		std::lock_guard lock(arenaHealthMutex);
		arenaHealthCache.clear();
	}

	logNetwork->info("Game state was transferred over network, loading.");
	gamestate = initializedGameState;
	gamestate->preInit(LIBRARY);
	gamestate->updateOnLoad(*GAME->server().si);
	logNetwork->info("Game loaded, initialize interfaces.");

	initMapHandler();
	reinitScripting();
	initPlayerEnvironments();
	initPlayerInterfaces();
}

void CClient::updateArenaHealth(const SetArenaHealth & pack)
{
	std::lock_guard lock(arenaHealthMutex);
	if(pack.mode == ChangeValueMode::ABSOLUTE)
	{
		arenaHealthCache[pack.player] = pack.health;
		return;
	}

	const auto existing = arenaHealthCache.find(pack.player);
	if(existing != arenaHealthCache.end())
		existing->second += pack.health;
}

std::optional<int32_t> CClient::getArenaHealth(PlayerColor player) const
{
	std::lock_guard lock(arenaHealthMutex);
	const auto existing = arenaHealthCache.find(player);
	if(existing == arenaHealthCache.end())
		return std::nullopt;
	return existing->second;
}

void CClient::endNetwork()
{
	if(!settings["session"]["headless"].Bool())
		GAME->map().endNetwork();

	if (CPlayerInterface::battleInt)
		CPlayerInterface::battleInt->endNetwork();

	for(auto & i : playerint)
	{
		auto interface = std::dynamic_pointer_cast<CPlayerInterface>(i.second);
		if (interface)
			interface->endNetwork();
	}
}

void CClient::finishGameplay()
{
	waitingRequest.requestTermination();

	//suggest interfaces to finish their stuff (AI should interrupt any bg working threads)
	for(auto & i : playerint)
		i.second->finish();
}

void CClient::endGame()
{
#if SCRIPTING_ENABLED
	clientScripts.reset();
#endif

	logNetwork->info("Ending current game!");
	removeGUI();

	GAME->setMapInstance(nullptr);

	logNetwork->info("Deleted mapHandler and gameState.");

	CPlayerInterface::battleInt.reset();
	playerint.clear();
	battleints.clear();
	battleCallbacks.clear();
	playerEnvironments.clear();

	//FIXME: gamestate->currentBattles.clear() will use gamestate. So it shoule be callded before gamestate.reset()
	gamestate->currentBattles.clear();
	gamestate.reset();

	logNetwork->info("Deleted playerInts.");
	logNetwork->info("Client stopped.");
}

void CClient::initMapHandler()
{
	// TODO: CMapHandler initialization can probably go somewhere else
	// It's can't be before initialization of interfaces
	// During loading CPlayerInterface from serialized state it's depend on MH
	if(!settings["session"]["headless"].Bool())
	{
		GAME->setMapInstance(std::make_unique<CMapHandler>(&gameState().getMap()));
		logNetwork->trace("Creating mapHandler: %d ms", GAME->server().th->getDiff());
	}
}

void CClient::initPlayerEnvironments()
{
	playerEnvironments.clear();

	auto allPlayers = GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID);
	bool hasHumanPlayer = false;
	for(auto & color : allPlayers)
	{
		logNetwork->info("Preparing environment for player %s", color.toString());
		playerEnvironments[color] = std::make_shared<CPlayerEnvironment>(color, this, std::make_shared<CCallback>(gamestate, color, this));
		
		if(color.isValidPlayer() && !hasHumanPlayer && gameState().players.at(color).isHuman())
			hasHumanPlayer = true;
	}

	const bool unattendedTest = !settings["session"]["testmap"].isNull();
	if(unattendedTest)
	{
		// Headless test maps must not allocate UI. A visible test map preserves an
		// explicit --spectate request for recorded-action playback.
		if(settings["session"]["headless"].Bool())
		{
			Settings session = settings.write["session"];
			session["spectate"].Bool() = false;
		}
	}
	else if(!hasHumanPlayer && !settings["session"]["headless"].Bool())
	{
		Settings session = settings.write["session"];
		session["spectate"].Bool() = true;
		session["spectate-skip-battle-result"].Bool() = true;
		session["spectate-ignore-hero"].Bool() = true;
	}
	
	if(settings["session"]["spectate"].Bool())
	{
		playerEnvironments[PlayerColor::SPECTATOR] = std::make_shared<CPlayerEnvironment>(PlayerColor::SPECTATOR, this, std::make_shared<CCallback>(gamestate, std::nullopt, this));
	}
}

void CClient::initPlayerInterfaces()
{
	for(const auto & playerInfo : gameState().getStartInfo()->playerInfos)
	{
		PlayerColor color = playerInfo.first;
		if(!vstd::contains(GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID), color))
			continue;

		if(!vstd::contains(playerint, color))
		{
			logNetwork->info("Preparing interface for player %s", color.toString());
			if(playerInfo.second.isControlledByAI() || settings["session"]["onlyai"].Bool())
			{
#ifdef VCMI_REMOTE_CLIENT_ONLY
				installNewPlayerInterface(CDynLibHandler::getNewServerDrivenArenaAI(), color);
#else
				bool alliedToHuman = false;
				for(const auto & allyInfo : gameState().getStartInfo()->playerInfos)
					if (gameState().getPlayerTeam(allyInfo.first) == gameState().getPlayerTeam(playerInfo.first) && allyInfo.second.isControlledByHuman())
						alliedToHuman = true;

				auto AiToGive = aiNameForPlayer(playerInfo.second, false, alliedToHuman);
				// Arena deployment has its own turn adapter in Nullkiller2. EmptyAI would
				// only end an ordinary turn and the regular planner would try actions that
				// are invalid in battle-only mode.
				if(gameState().getMapHeader()->battleOnly)
					AiToGive = "Nullkiller2";
				logNetwork->info("Player %s will be lead by %s", color.toString(), AiToGive);
				installNewPlayerInterface(CDynLibHandler::getNewAI(AiToGive), color);
#endif

				// Deployment adapters do not make combat decisions. Keep BattleAI/MMAI
				// independent from the adventure interface selected above.
				if(gameState().getMapHeader()->battleOnly)
				{
					const auto & playerCombatAI = settings["session"]["combatAIByPlayer"][std::to_string(color.getNum())];
					const auto & combatAIOverride = settings["session"]["combatAI"];
					const auto & serverCombatAI = gameState().getSettings().getValue(EGameSettings::ARENA_COMBAT_AI);
					const bool hasServerOverride = !serverCombatAI.isString() || !serverCombatAI.String().empty();
					if(hasServerOverride && (!serverCombatAI.isString() || (serverCombatAI.String() != "BattleAI" && serverCombatAI.String() != "MMAI")))
						throw std::runtime_error("Invalid server arena combat AI");
					const bool hasPlayerOverride = playerCombatAI.isString() && !playerCombatAI.String().empty();
					const bool hasProcessOverride = combatAIOverride.isString() && !combatAIOverride.String().empty();
#ifdef VCMI_REMOTE_CLIENT_ONLY
					const bool needsBattleAI = true;
#else
					const bool needsBattleAI = hasServerOverride || hasPlayerOverride || hasProcessOverride || GAME->server().loadMode == ELoadMode::SINGLE;
#endif
					if(needsBattleAI)
					{
						const std::string battleAIName = hasServerOverride ? serverCombatAI.String() : hasPlayerOverride
							? playerCombatAI.String()
							: (hasProcessOverride ? combatAIOverride.String() : settings["ai"]["combatEnemyAI"].String());
						logNetwork->info("Arena player %s will use %s in battle", color.toString(), battleAIName);
						installNewBattleInterface(CDynLibHandler::getNewBattleAI(battleAIName), color);
					}
				}
			}
			else
			{
				logNetwork->info("Player %s will be lead by human", color.toString());
				installNewPlayerInterface(std::make_shared<CPlayerInterface>(color), color);
			}

			if(!advInterfaceReadySent.contains(color))
			{
				advInterfaceReadySent.insert(color);
				AdvInterfaceReady air;
				sendRequest(air, color, /* waitTillRealize = */ false);
			}
		}
	}

	if(settings["session"]["spectate"].Bool())
	{
		installNewPlayerInterface(std::make_shared<CPlayerInterface>(PlayerColor::SPECTATOR), PlayerColor::SPECTATOR, true);
	}

	if(GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID).count(PlayerColor::NEUTRAL))
		installNewBattleInterface(CDynLibHandler::getNewBattleAI(settings["ai"]["combatNeutralAI"].String()), PlayerColor::NEUTRAL);

	restoreBattleAILocalState();

	logNetwork->trace("Initialized player interfaces %d ms", GAME->server().th->getDiff());
}

JsonNode CClient::prepareLocalStateForSave(PlayerColor player)
{
	const auto * playerState = gameState().getPlayerState(player, false);
	if(!playerState)
		return JsonNode();

	JsonNode data = *playerState->playerLocalSettings;
	const auto playerInterface = playerint.find(player);
	if(playerInterface != playerint.end())
		if(const auto humanInterface = std::dynamic_pointer_cast<CPlayerInterface>(playerInterface->second))
			humanInterface->localState->serialize(data);

	for(const auto & [color, battleInterface] : battleints)
	{
		if(!battleInterface || battleInterface->dllName != "MMAI")
			continue;
		JsonNode mmaiState;
		battleInterface->saveLocalState(mmaiState);
		if(!mmaiState.isNull())
			data["battleAI"]["MMAI"] = std::move(mmaiState);
		break; // MMAI's attacker/defender repository is shared by its routers.
	}

	return data;
}

void CClient::restoreBattleAILocalState()
{
	std::shared_ptr<CBattleGameInterface> mmai;
	for(const auto & [color, battleInterface] : battleints)
		if(battleInterface && battleInterface->dllName == "MMAI")
		{
			mmai = battleInterface;
			break;
		}
	if(!mmai)
		return;

	const JsonNode * savedState = nullptr;
	for(const PlayerColor color : GAME->server().getAllClientPlayers(GAME->server().logicConnection->connectionID))
	{
		const auto * playerState = gameState().getPlayerState(color, false);
		if(!playerState)
			continue;
		const auto & candidate = (*playerState->playerLocalSettings)["battleAI"]["MMAI"];
		if(!candidate.isNull())
		{
			savedState = &candidate;
			break;
		}
	}

	// A missing node is an old save or a new game. MMAI resets to its configured
	// initial seed so a repository left alive by another game cannot leak state.
	mmai->loadLocalState(savedState ? *savedState : JsonNode());
}

std::string CClient::aiNameForPlayer(const PlayerSettings & ps, bool battleAI, bool alliedToHuman) const
{
	if(ps.name.size())
	{
		const boost::filesystem::path aiPath = VCMIDirs::get().fullLibraryPath("AI", ps.name);
		if(boost::filesystem::exists(aiPath))
			return ps.name;
	}

	return aiNameForPlayer(battleAI, alliedToHuman);
}

std::string CClient::aiNameForPlayer(bool battleAI, bool alliedToHuman) const
{
	const int sensibleAILimit = settings["session"]["oneGoodAI"].Bool() ? 1 : PlayerColor::PLAYER_LIMIT_I;
	std::string goodAdventureAI = alliedToHuman ? settings["ai"]["adventureAlliedAI"].String() : settings["ai"]["adventureEnemyAI"].String();
	std::string goodBattleAI = settings["ai"]["combatNeutralAI"].String();
	std::string goodAI = battleAI ? goodBattleAI : goodAdventureAI;
	std::string badAI = battleAI ? "StupidAI" : "EmptyAI";

	//TODO what about human players
	if(battleints.size() >= sensibleAILimit)
		return badAI;

	return goodAI;
}

void CClient::installNewPlayerInterface(std::shared_ptr<CGameInterface> gameInterface, PlayerColor color, bool battlecb)
{
	playerint[color] = gameInterface;

	logGlobal->trace("\tInitializing the interface for player %s", color.toString());
	auto cb = std::make_shared<CCallback>(gamestate, color, this);
	battleCallbacks[color] = cb;
	gameInterface->initGameInterface(playerEnvironments.at(color), cb);

	installNewBattleInterface(gameInterface, color, battlecb);
}

void CClient::installNewBattleInterface(std::shared_ptr<CBattleGameInterface> battleInterface, PlayerColor color, bool needCallback)
{
	battleints[color] = battleInterface;

	if(needCallback)
	{
		logGlobal->trace("\tInitializing the battle interface for player %s", color.toString());
		auto cbc = std::make_shared<CBattleCallback>(color, this);
		battleCallbacks[color] = cbc;
		if(const char * probeOverride = std::getenv("VCMI_ARENA_BATTLE_PROBE_ENABLE_SPELLS"))
		{
			AutocombatPreferences preferences;
			const std::string value(probeOverride);
			if(value == "0")
				preferences.enableSpellsUsage = false;
			else if(value == "1")
				preferences.enableSpellsUsage = true;
			else
				throw std::runtime_error("VCMI_ARENA_BATTLE_PROBE_ENABLE_SPELLS must be '0' or '1'");
			logGlobal->info("[ARENA-PROBE] battle AI spells override for %s: %s", color.toString(),
				preferences.enableSpellsUsage ? "enabled" : "disabled");
			battleInterface->initBattleInterface(playerEnvironments.at(color), cbc, preferences);
		}
		else
			battleInterface->initBattleInterface(playerEnvironments.at(color), cbc);
	}
}

void CClient::handlePack(CPackForClient & pack)
{
	ApplyClientNetPackVisitor afterVisitor(*this, gameState());
	ApplyFirstClientNetPackVisitor beforeVisitor(*this, gameState());

	pack.visit(beforeVisitor);
	logNetwork->trace("\tMade first apply on cl: %s", typeid(pack).name());
	{
		std::unique_lock lock(CGameState::mutex);
		gameState().apply(pack);
	}
	logNetwork->trace("\tApplied on gs: %s", typeid(pack).name());
	pack.visit(afterVisitor);
	logNetwork->trace("\tMade second apply on cl: %s", typeid(pack).name());
}

std::optional<BattleAction> CClient::makeSurrenderRetreatDecision(PlayerColor player, const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	return playerint[player]->makeSurrenderRetreatDecision(battleID, battleState);
}

int CClient::sendRequest(const CPackForServer & request, PlayerColor player, bool waitTillRealize)
{
	ui32 requestID = requestCounter++;
	logNetwork->trace("Sending a request \"%s\". It'll have an ID=%d.", typeid(request).name(), requestID);

	waitingRequest.pushBack(requestID);
	request.requestID = requestID;
	request.player = player;
	GAME->server().sendGamePack(request);
	if(vstd::contains(playerint, player))
		playerint[player]->requestSent(&request, requestID);

	if(waitTillRealize)
	{
		logGlobal->trace("We'll wait till request %d is answered.\n", requestID);
		auto gsUnlocker = vstd::makeUnlockSharedGuard(CGameState::mutex);
		waitingRequest.waitWhileContains(requestID);
	}

	return requestID;
}

void CClient::battleStarted(const BattleID & battleID)
{
	const BattleInfo * info = gameState().getBattle(battleID);

	std::shared_ptr<CPlayerInterface> att;
	std::shared_ptr<CPlayerInterface> def;
	const auto & leftSide = info->getSide(BattleSide::LEFT_SIDE);
	const auto & rightSide = info->getSide(BattleSide::RIGHT_SIDE);

	for(auto & battleCb : battleCallbacks)
	{
		if(!battleCb.first.isValidPlayer() || battleCb.first == leftSide.color || battleCb.first == rightSide.color)
			battleCb.second->onBattleStarted(info);
	}

	// A single-player arena deliberately uses Nullkiller2 for deployment and a
	// separately configured BattleAI/MMAI for combat. In that setup playerint is
	// no longer present in battleints, so its battleStart override never runs.
	// The armies are public at this point (BattleStart follows arena reveal), and
	// the deployment planner needs the enemy snapshot for the next day's scoring.
	if(gameState().getMapHeader()->battleOnly)
	{
		const auto heroFightingStrength = [](const CGHeroInstance * hero)
		{
			return hero ? hero->getFightingStrength() : 1.0;
		};
		auto notifyDeploymentPlanner = [this, &heroFightingStrength](PlayerColor color, const CCreatureSet * ownArmy,
			const CCreatureSet * enemyArmy, const CGHeroInstance * ownHero, const CGHeroInstance * enemyHero)
		{
			const auto interface = playerint.find(color);
			if(interface != playerint.end())
				interface->second->arenaBattleStartedObservation(ownArmy, enemyArmy,
					heroFightingStrength(ownHero), heroFightingStrength(enemyHero));
		};
		notifyDeploymentPlanner(leftSide.color, leftSide.getArmy(), rightSide.getArmy(), leftSide.getHero(), rightSide.getHero());
		notifyDeploymentPlanner(rightSide.color, rightSide.getArmy(), leftSide.getArmy(), rightSide.getHero(), leftSide.getHero());
	}

	//If quick combat is not, do not prepare interfaces for battleint
	auto callBattleStart = [&](PlayerColor color, BattleSide side)
	{
		if(vstd::contains(battleints, color))
			battleints[color]->battleStart(info->battleID, leftSide.getArmy(), rightSide.getArmy(), info->tile, leftSide.getHero(), rightSide.getHero(), side, info->replayAllowed);
	};
	
	callBattleStart(leftSide.color, BattleSide::LEFT_SIDE);
	callBattleStart(rightSide.color, BattleSide::RIGHT_SIDE);
	callBattleStart(PlayerColor::UNFLAGGABLE, BattleSide::RIGHT_SIDE);
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		callBattleStart(PlayerColor::SPECTATOR, BattleSide::RIGHT_SIDE);
	
	if(vstd::contains(playerint, leftSide.color) && playerint[leftSide.color]->human)
		att = std::dynamic_pointer_cast<CPlayerInterface>(playerint[leftSide.color]);

	if(vstd::contains(playerint, rightSide.color) && playerint[rightSide.color]->human)
		def = std::dynamic_pointer_cast<CPlayerInterface>(playerint[rightSide.color]);
	
	//Remove player interfaces for auto battle (quickCombat option)
	if((att && att->isAutoFightOn) || (def && def->isAutoFightOn))
	{
		auto endTacticPhaseIfEligible = [info](const CPlayerInterface * interface)
		{
			if (interface->cb->getBattle(info->battleID)->battleGetTacticDist())
			{
				auto side = interface->cb->getBattle(info->battleID)->playerToSide(interface->playerID);

				if(interface->playerID == info->getSide(info->tacticsSide).color)
				{
					auto action = BattleAction::makeEndOFTacticPhase(side);
					interface->cb->battleMakeTacticAction(info->battleID, action);
				}
			}
		};

		if(att && att->isAutoFightOn)
			endTacticPhaseIfEligible(att.get());
		else // def && def->isAutoFightOn
			endTacticPhaseIfEligible(def.get());

		att.reset();
		def.reset();
	}

	if(!settings["session"]["headless"].Bool())
	{
		if(att || def)
		{
			CPlayerInterface::battleInt = std::make_shared<BattleInterface>(info->getBattleID(), leftSide.getArmy(), rightSide.getArmy(), leftSide.getHero(), rightSide.getHero(), att, def);
		}
		else if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		{
			//TODO: This certainly need improvement
			auto spectratorInt = std::dynamic_pointer_cast<CPlayerInterface>(playerint[PlayerColor::SPECTATOR]);
			spectratorInt->cb->onBattleStarted(info);
			CPlayerInterface::battleInt = std::make_shared<BattleInterface>(info->getBattleID(), leftSide.getArmy(), rightSide.getArmy(), leftSide.getHero(), rightSide.getHero(), att, def, spectratorInt);
		}
	}

	if(info->tacticDistance)
	{
		auto tacticianColor = info->getSide(info->tacticsSide).color;

		if (vstd::contains(battleints, tacticianColor))
			battleints[tacticianColor]->yourTacticPhase(info->battleID, info->tacticDistance);
	}
}

void CClient::battleFinished(const BattleID & battleID)
{
	for(auto side : { BattleSide::ATTACKER, BattleSide::DEFENDER })
	{
		if(battleCallbacks.count(gameState().getBattle(battleID)->getSide(side).color))
			battleCallbacks[gameState().getBattle(battleID)->getSide(side).color]->onBattleEnded(battleID);
	}

	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		battleCallbacks[PlayerColor::SPECTATOR]->onBattleEnded(battleID);
}

void CClient::startPlayerBattleAction(const BattleID & battleID, PlayerColor color)
{
	if (battleints.count(color) == 0)
		return; // not our combat in MP

	auto battleint = battleints.at(color);
	if (!battleint->human)
	{
		// we want to avoid locking gamestate and causing UI to freeze while AI is making turn
		if(ENGINE)
		{
			auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			battleint->activeStack(battleID, gameState().getBattle(battleID)->battleGetStackByID(gameState().getBattle(battleID)->activeStack, false));
		}
		else
		{
			battleint->activeStack(battleID, gameState().getBattle(battleID)->battleGetStackByID(gameState().getBattle(battleID)->activeStack, false));
		}
	}
	else
	{
		battleint->activeStack(battleID, gameState().getBattle(battleID)->battleGetStackByID(gameState().getBattle(battleID)->activeStack, false));
	}
}

void CClient::reinitScripting()
{
	clientEventBus = std::make_unique<events::EventBus>();
#if SCRIPTING_ENABLED
	clientScripts.reset(new scripting::PoolImpl(this));
#endif
}

void CClient::removeGUI() const
{
	// CClient::endGame
	if(!settings["session"]["headless"].Bool() && ENGINE)
		ENGINE->windows().clear();
	adventureInt.reset();
	logGlobal->info("Removed GUI.");

	GAME->setInterfaceInstance(nullptr);
}

void CClient::registerBattleInterface(std::shared_ptr<IBattleEventsReceiver> battleEvents, PlayerColor color)
{
	additionalBattleInts[color].push_back(battleEvents);
}

void CClient::unregisterBattleInterface(std::shared_ptr<IBattleEventsReceiver> battleEvents, PlayerColor color)
{
	additionalBattleInts[color] -= battleEvents;
}
