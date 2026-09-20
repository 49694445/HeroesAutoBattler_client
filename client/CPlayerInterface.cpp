/*
 * CPlayerInterface.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CPlayerInterface.h"

#include <vcmi/Artifact.h>

#include "Client.h"
#include "../lib/IGameSettings.h"
#include "CServerHandler.h"
#include "HeroMovementController.h"
#include "PlayerLocalState.h"

#include "adventureMap/AdventureMapInterface.h"
#include "adventureMap/CInGameConsole.h"
#include "adventureMap/CList.h"

#include "battle/BattleEffectsController.h"
#include "battle/BattleFieldController.h"
#include "battle/BattleInterface.h"
#include "battle/BattleResultWindow.h"
#include "battle/BattleWindow.h"

#include "eventsSDL/InputHandler.h"
#include "eventsSDL/NotificationHandler.h"

#include "GameEngine.h"
#include "GameInstance.h"
#include "gui/CursorHandler.h"
#include "gui/WindowHandler.h"

#include "mainmenu/CMainMenu.h"
#include "mainmenu/CHighScoreScreen.h"
#include "mainmenu/CStatisticScreen.h"

#include "mapView/mapHandler.h"

#include "media/IMusicPlayer.h"
#include "media/ISoundPlayer.h"

#include "render/CAnimation.h"
#include "render/IImage.h"
#include "render/IRenderHandler.h"
#include "render/IScreenHandler.h"

#include "widgets/Buttons.h"
#include "widgets/CComponent.h"
#include "widgets/CGarrisonInt.h"

#include "windows/CCastleInterface.h"
#include "windows/CCreatureWindow.h"
#include "windows/CExchangeWindow.h"
#include "windows/CHeroWindow.h"
#include "windows/CKingdomInterface.h"
#include "windows/CMarketWindow.h"
#include "windows/CPuzzleWindow.h"
#include "windows/CQuestLog.h"
#include "windows/CSpellWindow.h"
#include "windows/CTutorialWindow.h"
#include "windows/GUIClasses.h"
#include "windows/InfoWindows.h"
#include "windows/settings/SettingsMainWindow.h"

#include "../lib/callback/CDynLibHandler.h"
#include "../lib/CConfigHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/CRandomGenerator.h"
#include "../lib/CStack.h"
#include "../lib/CStopWatch.h"
#include "../lib/CThreadHelper.h"
#include "../lib/GameConstants.h"
#include "../lib/RoadHandler.h"
#include "../lib/StartInfo.h"
#include "../lib/TerrainHandler.h"
#include "../lib/UnlockGuard.h"
#include "../lib/VCMIDirs.h"

#include "../lib/battle/CPlayerBattleCallback.h"

#include "../lib/bonuses/Limiters.h"
#include "../lib/bonuses/Propagators.h"
#include "../lib/bonuses/Updaters.h"

#include "../lib/callback/CCallback.h"

#include "../lib/gameState/CGameState.h"

#include "../lib/mapObjects/CGMarket.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/MiscObjects.h"
#include "../lib/mapObjects/ObjectTemplate.h"

#include "../lib/mapping/CMap.h"
#include "../lib/mapping/CMapHeader.h"

#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/networkPacks/PacksForServer.h"

#include "../lib/pathfinder/CGPathNode.h"
#include "../lib/pathfinder/PathfinderCache.h"
#include "../lib/pathfinder/PathfinderOptions.h"

#include "../lib/serializer/CTypeList.h"
#include "../lib/serializer/ESerializationVersion.h"

#include "../lib/spells/CSpellHandler.h"

#include "../lib/texts/TextOperations.h"

#include "../lib/filesystem/Filesystem.h"

#include <boost/lexical_cast.hpp>

// The macro below is used to mark functions that are called by client when game state changes.
// They all assume that interface mutex is locked.
#define EVENT_HANDLER_CALLED_BY_CLIENT

#define BATTLE_EVENT_POSSIBLE_RETURN	if (GAME->interface() != this) return; if (isAutoFightOn && !battleInt) return

std::shared_ptr<BattleInterface> CPlayerInterface::battleInt;

CPlayerInterface::CPlayerInterface(PlayerColor Player):
	localState(std::make_unique<PlayerLocalState>(*this)),
	movementController(std::make_unique<HeroMovementController>()),
	artifactController(std::make_unique<ArtifactsUIController>())
	
{
	logGlobal->trace("\tHuman player interface for player %s being constructed", Player.toString());
	GAME->setInterfaceInstance(this);
	playerID=Player;
	human=true;
	battleInt.reset();
	castleInt = nullptr;
	makingTurn = false;
	showingDialog = new ConditionalWait();
	cingconsole = new CInGameConsole();
	autosaveCount = 0;
	isAutoFightOn = false;
	isAutoFightEndBattle = false;
	ignoreEvents = false;
	hasQuickSave = checkQuickLoadingGame();
}

CPlayerInterface::~CPlayerInterface()
{
	logGlobal->trace("\tHuman player interface for player %s being destructed", playerID.toString());
	delete showingDialog;
	delete cingconsole;
	if (GAME->interface() == this)
		GAME->setInterfaceInstance(nullptr);
}

void CPlayerInterface::initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB)
{
	// Player interfaces may be initialized again after a quick load/reconnect.
	// Never carry an old private arena-town session into the new game state.
	arenaTownVisitTown = ObjectInstanceID::NONE;
	arenaTownVisitHero = ObjectInstanceID::NONE;
	arenaTownVisitPendingTown = ObjectInstanceID::NONE;
	arenaTownVisitRevision = 0;
	arenaTownSummonRevision = 0;
	arenaTownVisitActive = false;
	arenaTownVisitPending = false;
	arenaDailyResourcePresentationActive.store(false);
	arenaArtifactDraftActive.store(false);
	arenaTownSummons.clear();
	arenaTownBuildGateTown = ObjectInstanceID::NONE;
	arenaTownBuildGateVisitRevision = 0;
	arenaTownBuildGateRequired = BuildingID::NONE;
	arenaTownBuildGateLocked = false;
	arenaTownBuildGateValid = false;
	arenaBattleProgressBattle = BattleID::NONE;
	arenaBattleProgressRound = -1;
	arenaBattleProgress.clear();
	arenaPreviousOwnHero.reset();
	arenaPreviousEnemyHero.reset();
	const auto resetPlayer = playerID;
	const auto resetTown = castleInt && castleInt->town ? castleInt->town->id : ObjectInstanceID::NONE;
	if(resetTown != ObjectInstanceID::NONE)
	{
		ENGINE->dispatchMainThread([resetPlayer, resetTown]()
		{
			if(auto * current = GAME->interface(); current && current->playerID == resetPlayer)
				current->closeArenaTownWindow(resetTown);
		});
	}

	cb = CB;
	env = ENV;
	if(playerID == PlayerColor::SPECTATOR)
	{
		// A spectator owns no PlayerState, heroes or towns. It only needs the
		// global callback and battle UI; initializing the adventure interface
		// would dereference a deliberately absent spectator PlayerState.
		return;
	}

	pathfinderCache = std::make_unique<PathfinderCache>(cb.get(), PathfinderOptions(*cb));
	ENGINE->music().loadTerrainMusicThemes();
	initializeHeroTownList();

	adventureInt.reset(new AdventureMapInterface());
	adventureInt->onCurrentPlayerChanged(playerID);
}

std::shared_ptr<const CPathsInfo> CPlayerInterface::getPathsInfo(const CGHeroInstance * h)
{
	return pathfinderCache->getPathsInfo(h);
}

void CPlayerInterface::invalidatePaths()
{
	pathfinderCache->invalidatePaths();
}

void CPlayerInterface::closeAllDialogs()
{
	// remove all active dialogs that do not expect query answer
	while(true)
	{
		auto adventureWindow = ENGINE->windows().topWindow<AdventureMapInterface>();
		auto settingsWindow = ENGINE->windows().topWindow<SettingsMainWindow>();
		auto infoWindow = ENGINE->windows().topWindow<CInfoWindow>();
		auto topWindow = ENGINE->windows().topWindow<WindowBase>();

		if(adventureWindow != nullptr)
			break;

		if(infoWindow && infoWindow->ID != QueryID::NONE)
			break;

		if (settingsWindow)
		{
			settingsWindow->close();
			continue;
		}

		if (topWindow)
			topWindow->close();
		else
			ENGINE->windows().popWindows(1); // does not inherits from WindowBase, e.g. settings dialog
	}
}

void CPlayerInterface::playerEndsTurn(PlayerColor player)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (player == playerID)
	{
		makingTurn = false;
		closeAllDialogs();

		// remove all pending dialogs that do not expect query answer
		vstd::erase_if(dialogs, [](const std::shared_ptr<CInfoWindow> & window){
						   return window->ID == QueryID::NONE;
					   });
	}
}

void CPlayerInterface::playerStartsTurn(PlayerColor player)
{
	if(ENGINE->windows().findWindows<AdventureMapInterface>().empty())
	{
		// after map load - remove all active windows and replace them with adventure map
		ENGINE->windows().clear();
		ENGINE->windows().pushWindow(adventureInt);
	}

	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (player != playerID && GAME->interface() == this)
	{
		// Arena deployment is simultaneous and players may reach it at different
		// times while the other client is still answering a level-up or result
		// query. Waiting for that local dialog here blocks the network thread, so
		// authoritative timeout/close packets can never reach it. The opponent's
		// deployment turn also must not switch this client into enemy-turn UI.
		if(cb->getMapHeader()->battleOnly)
			return;

		waitWhileDialog();

		bool isHuman = cb->getStartInfo()->playerInfos.count(player) && cb->getStartInfo()->playerInfos.at(player).isControlledByHuman();

		if (makingTurn == false)
			adventureInt->onEnemyTurnStarted(player, isHuman);
	}
}

void CPlayerInterface::performAutosave()
{
	int frequency = static_cast<int>(settings["general"]["saveFrequency"].Integer());
	if(frequency > 0 && cb->getDate() % frequency == 0)
	{
		bool usePrefix = settings["general"]["useSavePrefix"].Bool();
		std::string prefix = std::string();

		if(usePrefix)
		{
			prefix = settings["general"]["savePrefix"].String();
			if(prefix.empty())
			{
				std::string name = cb->getMapHeader()->name.toString();
				int txtlen = TextOperations::getUnicodeCharactersCount(name);

				TextOperations::trimRightUnicode(name, std::max(0, txtlen - 14));
				auto const & isSymbolIllegal = [&](char c) {
					static const std::string forbiddenChars("\\/:*?\"<>| ");

					bool charForbidden = forbiddenChars.find(c) != std::string::npos;
					bool charNonprintable = static_cast<unsigned char>(c) < static_cast<unsigned char>(' ');

					return charForbidden || charNonprintable;
				};
				std::replace_if(name.begin(), name.end(), isSymbolIllegal, '_' );

				prefix = vstd::getFormattedDateTime(cb->getStartInfo()->startTime, "%Y-%m-%d_%H-%M") + "_" + name + "/";
			}
		}

		autosaveCount++;

		int autosaveCountLimit = settings["general"]["autosaveCountLimit"].Integer();
		if(autosaveCountLimit > 0)
		{
			cb->save("Saves/Autosave/" + prefix + std::to_string(autosaveCount), false);
			autosaveCount %= autosaveCountLimit;
		}
		else
		{
			std::string stringifiedDate = std::to_string(cb->getDate(Date::MONTH))
					+ std::to_string(cb->getDate(Date::WEEK))
					+ std::to_string(cb->getDate(Date::DAY_OF_WEEK));

			cb->save("Saves/Autosave/" + prefix + stringifiedDate, false);
		}
	}
}

void CPlayerInterface::gamePause(bool pause)
{
	cb->gamePause(pause);
}

void CPlayerInterface::setArenaDeploymentActive(bool active)
{
	if(cb->getMapHeader()->battleOnly)
	{
		arenaDeploymentActive.store(active);
		if(!active)
		{
			arenaDailyResourcePresentationActive.store(false);
			arenaArtifactDraftActive.store(false);
		}
	}
}

void CPlayerInterface::setArenaNextBattleTerrain(TerrainId terrain)
{
	if(cb->getMapHeader()->battleOnly)
		arenaNextBattleTerrain.store(terrain.getNum());
}

TerrainId CPlayerInterface::getArenaNextBattleTerrain() const
{
	return TerrainId(arenaNextBattleTerrain.load());
}

void CPlayerInterface::setArenaNextBattleSiege(bool siege)
{
	if(cb->getMapHeader()->battleOnly)
		arenaNextBattleSiege.store(siege);
}

bool CPlayerInterface::isArenaNextBattleSiege() const
{
	return arenaNextBattleSiege.load();
}

void CPlayerInterface::setArenaLastOrdinaryBattleLoss(PlayerColor loser, int32_t count, int32_t siegeInterval)
{
	if(!cb->getMapHeader()->battleOnly)
		return;
	arenaLastOrdinaryBattleLoser.store(loser.getNum());
	arenaConsecutiveOrdinaryLosses.store(std::max(0, count));
	arenaSiegeInterval.store(std::max(1, siegeInterval));
}

PlayerColor CPlayerInterface::getArenaLastOrdinaryBattleLoser() const
{
	return PlayerColor(arenaLastOrdinaryBattleLoser.load());
}

int32_t CPlayerInterface::getArenaConsecutiveOrdinaryLosses() const
{
	return arenaConsecutiveOrdinaryLosses.load();
}

int32_t CPlayerInterface::getArenaSiegeInterval() const
{
	return arenaSiegeInterval.load();
}

void CPlayerInterface::setArenaPreviousBattleHeroes(const std::optional<ArenaHeroSnapshot> & ownHero, const std::optional<ArenaHeroSnapshot> & enemyHero)
{
	if(!cb->getMapHeader()->battleOnly)
		return;
	arenaPreviousOwnHero = ownHero;
	arenaPreviousEnemyHero = enemyHero;
}

const std::optional<ArenaHeroSnapshot> & CPlayerInterface::getArenaPreviousOwnHero() const
{
	return arenaPreviousOwnHero;
}

const std::optional<ArenaHeroSnapshot> & CPlayerInterface::getArenaPreviousEnemyHero() const
{
	return arenaPreviousEnemyHero;
}

void CPlayerInterface::yourTurn(QueryID queryID)
{
	closeAllDialogs();
	CTutorialWindow::openWindowFirstTime(TutorialMode::TOUCH_ADVENTUREMAP);

	EVENT_HANDLER_CALLED_BY_CLIENT;

	int humanPlayersCount = 0;
	for(const auto & info : cb->getStartInfo()->playerInfos)
		if (info.second.isControlledByHuman())
			humanPlayersCount++;

	// Arena deployment is simultaneous. ArenaDeploymentStarted has already
	// activated this local interface before PlayerStartsTurn arrives, and the
	// Draft itself is the opening modal. Do not put the original sequential-turn
	// confirmation dialog between deployment activation and the Draft window.
	const bool arenaDeploymentTurn = cb->getMapHeader()->battleOnly && arenaDeploymentActive.load();
	bool hotseatWait = humanPlayersCount > 1 && !arenaDeploymentTurn;

		GAME->setInterfaceInstance(this);

		NotificationHandler::notify("Your turn");
		if(settings["general"]["startTurnAutosave"].Bool())
		{
			performAutosave();
		}

		if (hotseatWait) //hot seat or MP message
		{
			adventureInt->onHotseatWaitStarted(playerID);

			makingTurn = true;
			std::string msg = LIBRARY->generaltexth->allTexts[13];
			boost::replace_first(msg, "%s", cb->getStartInfo()->playerInfos.find(playerID)->second.name);
			std::vector<std::shared_ptr<CComponent>> cmp;
			cmp.push_back(std::make_shared<CComponent>(ComponentType::FLAG, playerID));
			showInfoDialog(msg, cmp);
		}
		else
		{
			makingTurn = true;
			adventureInt->onPlayerTurnStarted(playerID);
		}

	acceptTurn(queryID, hotseatWait);
}

void CPlayerInterface::acceptTurn(QueryID queryID, bool hotseatWait)
{
	if (settings["session"]["autoSkip"].Bool())
	{
		while(auto iw = ENGINE->windows().topWindow<CInfoWindow>())
			iw->close();
	}

	if(hotseatWait)
	{
		waitWhileDialog(); // wait for player to accept turn in hot-seat mode

		adventureInt->onPlayerTurnStarted(playerID);
	}

	// warn player if he has no town
	if (cb->howManyTowns() == 0)
	{
		auto playerColor = *cb->getPlayerID();

		std::vector<Component> components;
		components.emplace_back(ComponentType::FLAG, playerColor);
		MetaString text;

		const auto & optDaysWithoutCastle = cb->getPlayerState(playerColor)->daysWithoutCastle;

		if(optDaysWithoutCastle)
		{
			auto daysWithoutCastle = optDaysWithoutCastle.value();
			if (daysWithoutCastle < 6)
			{
				text.appendLocalString(EMetaText::ARRAY_TXT,128); //%s, you only have %d days left to capture a town or you will be banished from this land.
				text.replaceName(playerColor);
				text.replaceNumber(7 - daysWithoutCastle);
			}
			else if (daysWithoutCastle == 6)
			{
				text.appendLocalString(EMetaText::ARRAY_TXT,129); //%s, this is your last day to capture a town or you will be banished from this land.
				text.replaceName(playerColor);
			}

			showInfoDialogAndWait(components, text);
		}
		else
			logGlobal->warn("Player has no towns, but daysWithoutCastle is not set");
	}

	if (queryID.hasValue())
		cb->selectionMade(0, queryID);
	movementController->onPlayerTurnStarted();
}

void CPlayerInterface::heroMoved(const TryMoveHero & details, bool verbose)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	if(GAME->interface() != this)
		return;

	//FIXME: read once and store
	if(settings["session"]["spectate"].Bool() && settings["session"]["spectate-ignore-hero"].Bool())
		return;

	const CGHeroInstance * hero = cb->getHero(details.id); //object representing this hero

	if (!hero)
		return;

	movementController->onTryMoveHero(hero, details);
}

void CPlayerInterface::heroKilled(const CGHeroInstance* hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	LOG_TRACE_PARAMS(logGlobal, "Hero %s killed handler for player %s", hero->getNameTranslated() % playerID);

	// if hero is not in town garrison
	if (vstd::contains(localState->getWanderingHeroes(), hero))
		localState->removeWanderingHero(hero);

	adventureInt->onHeroChanged(hero);
	localState->erasePath(hero);
}

void CPlayerInterface::townRemoved(const CGTownInstance* town)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	if(town->tempOwner == playerID)
	{
		localState->removeOwnedTown(town);
		adventureInt->onTownChanged(town);
	}
}


void CPlayerInterface::heroVisit(const CGHeroInstance * visitor, const CGObjectInstance * visitedObj, bool start)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(start && visitedObj)
	{
		auto visitSound = visitedObj->getVisitSound(CRandomGenerator::getDefault());
		if (visitSound)
			ENGINE->sound().playSound(visitSound.value());
	}
}

void CPlayerInterface::heroCreated(const CGHeroInstance * hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	localState->addWanderingHero(hero);
	adventureInt->onHeroChanged(hero);
	if(castleInt)
		ENGINE->sound().playSound(soundBase::newBuilding);
}
void CPlayerInterface::openTownWindow(const CGTownInstance * town)
{
	if(castleInt)
		castleInt->close();
	castleInt = nullptr;

	auto newCastleInt = std::make_shared<CCastleInterface>(town);

	ENGINE->windows().pushWindow(newCastleInt);
}

void CPlayerInterface::requestTownWindow(const CGTownInstance * town)
{
	if(!cb->getMapHeader()->battleOnly || town->tempOwner != playerID)
	{
		openTownWindow(town);
		return;
	}

	// The result window can disappear just before the server's next-deployment
	// notification reaches this client. Do not turn clicks in that synchronization
	// gap into requests which the authoritative server must reject.
	if(!arenaDeploymentActive.load()
		|| arenaDailyResourcePresentationActive.load()
		|| arenaArtifactDraftActive.load())
		return;

	if(arenaTownVisitPending || (arenaTownVisitActive && arenaTownVisitTown == town->id))
		return;

	arenaTownVisitPending = true;
	arenaTownVisitPendingTown = town->id;
	ArenaTownVisitRequest request;
	request.action = ArenaTownVisitRequest::EAction::ENTER;
	request.town = town->id;
	request.expectedRevision = arenaTownVisitRevision;
	GAME->server().client->sendRequest(request, playerID, false);
}

bool CPlayerInterface::requestArenaTownLeave(const CGTownInstance * town)
{
	if(!cb->getMapHeader()->battleOnly || !arenaDeploymentActive.load()
		|| arenaDailyResourcePresentationActive.load() || arenaArtifactDraftActive.load()
		|| !arenaTownVisitActive || arenaTownVisitTown != town->id)
		return false;
	if(arenaTownVisitPending)
		return true;

	arenaTownVisitPending = true;
	arenaTownVisitPendingTown = town->id;
	ArenaTownVisitRequest request;
	request.action = ArenaTownVisitRequest::EAction::LEAVE;
	request.town = town->id;
	request.expectedRevision = arenaTownVisitRevision;
	GAME->server().client->sendRequest(request, playerID, false);
	return true;
}

bool CPlayerInterface::requestArenaTownSwitch(const CGTownInstance * town)
{
	if(!cb->getMapHeader()->battleOnly || !arenaDeploymentActive.load()
		|| arenaDailyResourcePresentationActive.load() || arenaArtifactDraftActive.load()
		|| !arenaTownVisitActive)
		return false;
	if(arenaTownVisitPending || arenaTownVisitTown == town->id)
		return true;

	arenaTownVisitPending = true;
	arenaTownVisitPendingTown = town->id;
	ArenaTownVisitRequest request;
	request.action = ArenaTownVisitRequest::EAction::SWITCH;
	request.town = town->id;
	request.expectedRevision = arenaTownVisitRevision;
	GAME->server().client->sendRequest(request, playerID, false);
	return true;
}

bool CPlayerInterface::requestArenaMainHero(const CGHeroInstance * hero)
{
	if(!hero || !cb->getMapHeader()->battleOnly || !arenaDeploymentActive.load() || hero->tempOwner != playerID)
		return false;

	ArenaSetMainHeroRequest request;
	request.hero = hero->id;
	GAME->server().client->sendRequest(request, playerID, false);
	return true;
}

bool CPlayerInterface::isArenaTownVisit(const CGTownInstance * town) const
{
	return cb->getMapHeader()->battleOnly && arenaTownVisitActive && town && arenaTownVisitTown == town->id;
}

void CPlayerInterface::applyArenaTownSummonState(const SetArenaTownSummonState & pack)
{
	if(pack.player != playerID || !cb->getMapHeader()->battleOnly || !arenaTownVisitActive || pack.town != arenaTownVisitTown || pack.mainHero != arenaTownVisitHero || pack.visitRevision != arenaTownVisitRevision)
	{
		return;
	}
	if(pack.summonRevision < arenaTownSummonRevision)
	{
		return;
	}
	if(pack.summonRevision > arenaTownSummonRevision)
	{
		arenaTownSummons.clear();
		arenaTownSummonRevision = pack.summonRevision;
	}

	auto existing = std::find_if(arenaTownSummons.begin(), arenaTownSummons.end(), [&pack](const auto & state)
	{
		return state.town == pack.town && state.building == pack.building && state.creature == pack.creature;
	});
	if(existing != arenaTownSummons.end() && pack.summonRevision < existing->summonRevision)
	{
		return;
	}

	ArenaTownSummonState state{pack.town, pack.building, pack.creature, pack.mainHero, pack.visitRevision, pack.summonRevision, pack.status, pack.amount, pack.cost, pack.reason};
	if(existing != arenaTownSummons.end())
		*existing = std::move(state);
	else
		arenaTownSummons.push_back(std::move(state));

	// This method is invoked by the single main-thread task queued in
	// visitSetArenaTownSummonState. Finish cache replacement and GUI refresh in
	// that same transaction instead of recursively dispatching another event.
	auto forts = ENGINE->windows().findWindows<CFortScreen>();
	for(auto fort : forts)
		if(fort->isForTown(pack.town))
			fort->refreshArenaTownSummons();
	const bool hasCastle = castleInt && castleInt->town && castleInt->town->id == pack.town;
	if(hasCastle)
		castleInt->refreshArenaTownSummons();
}

std::optional<CPlayerInterface::ArenaTownSummonState> CPlayerInterface::getArenaTownSummonState(const CGTownInstance * town, BuildingID building, CreatureID creature) const
{
	if(!isArenaTownVisit(town))
		return std::nullopt;

	const auto existing = std::find_if(arenaTownSummons.begin(), arenaTownSummons.end(), [town, building, creature, this](const auto & state)
	{
		return state.town == town->id && state.building == building && state.creature == creature && state.mainHero == arenaTownVisitHero && state.visitRevision == arenaTownVisitRevision;
	});
	if(existing == arenaTownSummons.end())
		return std::nullopt;
	return *existing;
}

bool CPlayerInterface::requestArenaTownSummon(const CGTownInstance * town, BuildingID building, CreatureID creature)
{
	if(!isArenaTownVisit(town))
		return false;

	auto existing = std::find_if(arenaTownSummons.begin(), arenaTownSummons.end(), [town, building, creature, this](const auto & state)
	{
		return state.town == town->id && state.building == building && state.creature == creature && state.mainHero == arenaTownVisitHero && state.visitRevision == arenaTownVisitRevision;
	});
	if(existing == arenaTownSummons.end() || existing->status != EArenaTownSummonStatus::AVAILABLE || existing->pending)
		return true;

	existing->pending = true;
	ArenaSummonFromTown request;
	request.town = town->id;
	request.creature = creature;
	request.expectedVisitRevision = arenaTownVisitRevision;
	request.expectedSummonRevision = existing->summonRevision;
	GAME->server().client->sendRequest(request, playerID, false);
	return true;
}

void CPlayerInterface::applyArenaTownVisitState(const SetArenaTownVisitState & pack)
{
	if(pack.player != playerID || pack.revision < arenaTownVisitRevision)
		return;
	if(pack.status == EArenaTownVisitStatus::ENTERED && (!arenaTownVisitPending || pack.town != arenaTownVisitPendingTown))
		return;
	if(pack.status == EArenaTownVisitStatus::LEFT && (!arenaTownVisitActive || pack.town != arenaTownVisitTown))
		return;

	if(pack.status == EArenaTownVisitStatus::REJECTED)
	{
		if(!arenaTownVisitPending || pack.town != arenaTownVisitPendingTown)
			return;
		arenaTownVisitRevision = pack.revision;
		arenaTownVisitPending = false;
		arenaTownVisitPendingTown = ObjectInstanceID::NONE;
		const auto reason = pack.reason;
		const auto rejectedPlayer = playerID;
		ENGINE->dispatchMainThread([reason, rejectedPlayer]()
		{
			if(!reason.empty())
				if(auto * current = GAME->interface(); current && current->playerID == rejectedPlayer)
					current->showInfoDialog(reason);
		});
		return;
	}

	arenaTownVisitRevision = pack.revision;
	arenaTownVisitPending = false;
	arenaTownVisitPendingTown = ObjectInstanceID::NONE;
	if(pack.status == EArenaTownVisitStatus::ENTERED)
	{
		arenaTownSummons.clear();
		arenaTownSummonRevision = 0;
		arenaTownVisitActive = true;
		arenaTownVisitTown = pack.town;
		arenaTownVisitHero = pack.mainHero;
		arenaTownBuildGateValid = false;
		arenaTownBuildGateTown = ObjectInstanceID::NONE;
		arenaTownFortCost = TResources{};
		return;
	}

	arenaTownVisitActive = false;
	arenaTownSummons.clear();
	arenaTownSummonRevision = 0;
	arenaTownVisitTown = ObjectInstanceID::NONE;
	arenaTownVisitHero = ObjectInstanceID::NONE;
	arenaTownBuildGateValid = false;
	arenaTownBuildGateTown = ObjectInstanceID::NONE;
	arenaTownFortCost = TResources{};
	const auto leftPlayer = playerID;
	const auto leftTown = pack.town;
	ENGINE->dispatchMainThread([leftPlayer, leftTown]()
	{
		if(auto * current = GAME->interface(); current && current->playerID == leftPlayer)
			current->closeArenaTownWindow(leftTown);
	});
}

void CPlayerInterface::applyArenaTownBuildGateState(const SetArenaTownBuildGateState & pack)
{
	if(pack.player != playerID || !arenaTownVisitActive || pack.town != arenaTownVisitTown || pack.visitRevision != arenaTownVisitRevision)
		return;

	arenaTownBuildGateTown = pack.town;
	arenaTownBuildGateVisitRevision = pack.visitRevision;
	arenaTownBuildGateRequired = pack.requiredBuilding;
	arenaTownBuildGateLocked = pack.locked;
	arenaTownFortCost = pack.fortCost;
	arenaTownBuildGateValid = true;

	const auto gatePlayer = playerID;
	const auto gateTown = pack.town;
	const auto gateRevision = pack.visitRevision;
	ENGINE->dispatchMainThread([gatePlayer, gateTown, gateRevision]()
	{
		auto * current = GAME->interface();
		if(!current || current->playerID != gatePlayer || !current->arenaTownVisitActive || current->arenaTownVisitTown != gateTown || current->arenaTownVisitRevision != gateRevision)
			return;
		for(auto hall : ENGINE->windows().findWindows<CHallInterface>())
			if(hall->isForTown(gateTown))
				hall->refreshArenaBuildGate();
		for(auto buildWindow : ENGINE->windows().findWindows<CBuildWindow>())
			if(buildWindow->isForTown(gateTown))
				buildWindow->refreshArenaBuildGate();
	});
}

bool CPlayerInterface::isArenaTownBuildGateLocked(const CGTownInstance * town, BuildingID building, BuildingID * requiredBuilding) const
{
	if(requiredBuilding)
		*requiredBuilding = BuildingID::NONE;
	if(!arenaTownBuildGateValid || !arenaTownBuildGateLocked || !town || town->id != arenaTownBuildGateTown || arenaTownBuildGateVisitRevision != arenaTownVisitRevision)
		return false;
	if(requiredBuilding)
		*requiredBuilding = arenaTownBuildGateRequired;
	return building != arenaTownBuildGateRequired;
}

std::optional<TResources> CPlayerInterface::getArenaTownFortCost(const CGTownInstance * town) const
{
	if(!arenaTownBuildGateValid || !town || town->id != arenaTownBuildGateTown
		|| arenaTownBuildGateVisitRevision != arenaTownVisitRevision)
		return std::nullopt;
	return arenaTownFortCost;
}

void CPlayerInterface::applyArenaBattleCreatureProgress(const SetArenaBattleCreatureProgress & pack)
{
	if(!cb->getMapHeader()->battleOnly || !battleInt || battleInt->getBattleID() != pack.battleID || pack.completedRound < 0 || (arenaBattleProgressBattle == pack.battleID && pack.completedRound < arenaBattleProgressRound))
		return;

	arenaBattleProgressBattle = pack.battleID;
	arenaBattleProgressRound = pack.completedRound;
	arenaBattleProgress.clear();
	for(const auto & entry : pack.entries)
		arenaBattleProgress.push_back({entry.player, entry.creature, entry.level, entry.currentExperience, entry.maximumExperience});

	const auto battleID = pack.battleID;
	ENGINE->dispatchMainThread([battleID]()
	{
		for(auto window : ENGINE->windows().findWindows<CStackWindow>())
			window->refreshArenaBattleProgress();
	});
}

void CPlayerInterface::clearArenaBattleCreatureProgress(BattleID battleID)
{
	if(arenaBattleProgressBattle != battleID)
		return;
	arenaBattleProgressBattle = BattleID::NONE;
	arenaBattleProgressRound = -1;
	arenaBattleProgress.clear();
}

std::optional<CPlayerInterface::ArenaBattleCreatureProgress> CPlayerInterface::getArenaBattleCreatureProgress(BattleID battleID, PlayerColor player, CreatureID creature) const
{
	if(arenaBattleProgressBattle != battleID)
		return std::nullopt;
	for(const auto & entry : arenaBattleProgress)
		if(entry.player == player && entry.creature == creature)
			return entry;
	return std::nullopt;
}

void CPlayerInterface::closeArenaTownWindow(ObjectInstanceID town)
{
	if(!castleInt)
		return;

	// A leave confirmation can race closeAllDialogs() during a forced deployment
	// end. Only the current top window may be closed, and a missing window means
	// that the leave was already handled.
	const auto castleWindows = ENGINE->windows().findWindows<CCastleInterface>();
	const auto stillOpen = std::any_of(castleWindows.begin(), castleWindows.end(), [this](const auto & window)
	{
		return window.get() == castleInt;
	});
	if(!stillOpen)
		return;
	if(!castleInt->town || castleInt->town->id != town)
		return;
	if(ENGINE->windows().isTopWindow(castleInt))
		castleInt->closeAfterArenaLeave();
}

void CPlayerInterface::heroExperienceChanged(const CGHeroInstance * hero, si64 val)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	for(auto ctw : ENGINE->windows().findWindows<IMarketHolder>())
		ctw->updateExperience();
}

void CPlayerInterface::heroPrimarySkillChanged(const CGHeroInstance * hero, PrimarySkill which, si64 val)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(hero);
}

void CPlayerInterface::heroSecondarySkillChanged(const CGHeroInstance * hero, int which, int val)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	for (auto cuw : ENGINE->windows().findWindows<IMarketHolder>())
		cuw->updateSecondarySkills();

	localState->verifyPath(hero);
	adventureInt->onHeroChanged(hero);// secondary skill can change primary skill / mana limit
}

void CPlayerInterface::heroManaPointsChanged(const CGHeroInstance * hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(hero);
	if (makingTurn && hero->tempOwner == playerID)
		adventureInt->onHeroChanged(hero);
}
void CPlayerInterface::heroMovePointsChanged(const CGHeroInstance * hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (makingTurn && hero->tempOwner == playerID)
		adventureInt->onHeroChanged(hero);
	invalidatePaths();
	localState->verifyPath(hero);
}
void CPlayerInterface::receivedResource()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	for (auto mw : ENGINE->windows().findWindows<IMarketHolder>())
		mw->updateResources();

	ENGINE->windows().totalRedraw();
}

void CPlayerInterface::heroGotLevel(const CGHeroInstance *hero, PrimarySkill pskill, std::vector<SecondarySkill>& skills, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	int32_t arenaTimeoutSeconds = -1;
	if(cb->getMapHeader()->battleOnly)
	{
		const auto it = arenaHeroLevelUpChoiceTimeouts.find(queryID);
		if(it != arenaHeroLevelUpChoiceTimeouts.end())
			arenaTimeoutSeconds = it->second;
		if(arenaTimeoutSeconds == 0)
			return; // Server has already taken the choice; never send a client answer.
	}
	waitWhileDialog();
	ENGINE->sound().playSound(soundBase::heroNewLevel);
	ENGINE->windows().createAndPushWindow<CLevelWindow>(hero, pskill, skills, queryID, arenaTimeoutSeconds, [this, queryID](ui32 selection)
	{
		arenaHeroLevelUpChoiceTimeouts.erase(queryID);
		if(queryID < 0)
			return;

		cb->selectionMade(selection, queryID);
	});
}

void CPlayerInterface::setArenaHeroLevelUpChoiceTimeout(QueryID queryID, int32_t remainingSeconds)
{
	if(remainingSeconds < 0)
	{
		arenaHeroLevelUpChoiceTimeouts.erase(queryID);
		return;
	}

	arenaHeroLevelUpChoiceTimeouts[queryID] = remainingSeconds;
	ENGINE->dispatchMainThread([queryID, remainingSeconds]()
	{
		auto * interface = GAME->interface();
		if(!interface)
			return;

		for(auto window : ENGINE->windows().findWindows<CLevelWindow>())
		{
			if(window->setArenaTimeout(queryID, remainingSeconds) && remainingSeconds == 0)
				window->closeFromServer();
		}
	});
}

void CPlayerInterface::setArenaBattleResultTimeout(QueryID queryID, int32_t remainingSeconds)
{
	if(remainingSeconds < 0)
	{
		arenaBattleResultTimeouts.erase(queryID);
		return;
	}

	if(remainingSeconds == 0)
		arenaBattleResultTimeouts.erase(queryID);
	else
		arenaBattleResultTimeouts[queryID] = remainingSeconds;
	ENGINE->dispatchMainThread([queryID, remainingSeconds]()
	{
		auto * interface = GAME->interface();
		if(!interface)
			return;

		for(auto window : ENGINE->windows().findWindows<BattleResultWindow>())
		{
			if(window->setArenaTimeout(queryID, remainingSeconds) && remainingSeconds == 0)
				window->closeAfterArenaServerConfirmation();
		}
	});
}

int32_t CPlayerInterface::getArenaBattleResultTimeout(QueryID queryID) const
{
	const auto it = arenaBattleResultTimeouts.find(queryID);
	return it == arenaBattleResultTimeouts.end() ? -1 : it->second;
}

void CPlayerInterface::commanderGotLevel (const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	ENGINE->sound().playSound(soundBase::heroNewLevel);
	ENGINE->windows().createAndPushWindow<CStackWindow>(commander, skills, [this, queryID](ui32 selection)
	{
		if(queryID < 0)
			return;

		cb->selectionMade(selection, queryID);
	});
}

void CPlayerInterface::heroInGarrisonChange(const CGTownInstance *town)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	if(town->getGarrisonHero()) //wandering hero moved to the garrison
	{
		// This method also gets called on hero recruitment -> garrisoned hero is already in garrison
		if(town->getGarrisonHero()->tempOwner == playerID && vstd::contains(localState->getWanderingHeroes(), town->getGarrisonHero()))
			localState->removeWanderingHero(town->getGarrisonHero());
	}

	if(town->getVisitingHero()) //hero leaves garrison
	{
		// This method also gets called on hero recruitment -> wandering heroes already contains new hero
		if(town->getVisitingHero()->tempOwner == playerID && !vstd::contains(localState->getWanderingHeroes(), town->getVisitingHero()))
			localState->addWanderingHero(town->getVisitingHero());
	}
	adventureInt->onHeroChanged(nullptr);
	adventureInt->onTownChanged(town);

	for (auto cgh : ENGINE->windows().findWindows<IGarrisonHolder>())
		if (cgh->holdsGarrison(town))
			cgh->updateGarrisons();

	for (auto ki : ENGINE->windows().findWindows<CKingdomInterface>())
		ki->townChanged(town);

	// Perform totalRedraw to update hero list on adventure map, if any dialogs are open
	ENGINE->windows().totalRedraw();
}

void CPlayerInterface::heroVisitsTown(const CGHeroInstance* hero, const CGTownInstance * town)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (hero->tempOwner != playerID )
		return;
	if(cb->getMapHeader()->battleOnly && (!arenaTownVisitActive || arenaTownVisitTown != town->id || arenaTownVisitHero != hero->id))
		return;

	waitWhileDialog();
	openTownWindow(town);
}

void CPlayerInterface::garrisonsChanged(ObjectInstanceID id1, ObjectInstanceID id2)
{
	std::vector<const CArmedInstance *> instances;

	if(auto obj = dynamic_cast<const CArmedInstance *>(cb->getObjInstance(id1)))
		instances.push_back(obj);


	if(id2 != ObjectInstanceID() && id2 != id1)
	{
		if(auto obj = dynamic_cast<const CArmedInstance *>(cb->getObjInstance(id2)))
			instances.push_back(obj);
	}

	garrisonsChanged(instances);
}

void CPlayerInterface::garrisonsChanged(std::vector<const CArmedInstance *> objs)
{
	for (auto object : objs)
	{
		auto * hero = dynamic_cast<const CGHeroInstance*>(object);
		auto * town = dynamic_cast<const CGTownInstance*>(object);

		if (town)
			adventureInt->onTownChanged(town);

		if (hero)
		{
			localState->verifyPath(hero);

			adventureInt->onHeroChanged(hero);
			if(hero->isGarrisoned() && hero->getVisitedTown() != town)
				adventureInt->onTownChanged(hero->getVisitedTown());
		}
	}

	for (auto cgh : ENGINE->windows().findWindows<IGarrisonHolder>())
		if (cgh->holdsGarrisons(objs))
			cgh->updateGarrisons();

	ENGINE->windows().totalRedraw();
}

void CPlayerInterface::buildChanged(const CGTownInstance *town, BuildingID buildingID, int what) //what: 1 - built, 2 - demolished
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onTownChanged(town);

	if (castleInt)
	{
		castleInt->townlist->updateElement(town);

		if (castleInt->town == town)
		{
			switch(what)
			{
			case 1:
				castleInt->addBuilding(buildingID);
				break;
			case 2:
				castleInt->removeBuilding(buildingID);
				break;
			}
		}

		// Perform totalRedraw in order to force redraw of updated town list icon from adventure map
		ENGINE->windows().totalRedraw();
	}

	for (auto cgh : ENGINE->windows().findWindows<ITownHolder>())
		cgh->buildChanged();
}

void CPlayerInterface::battleStartBefore(const BattleID & battleID, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2)
{
	if(playerID.isValidPlayer() && cb->getMapHeader()->battleOnly && ENGINE->windows().findWindows<AdventureMapInterface>().empty())
	{
		// Battle-only mode can start its first battle before PlayerStartsTurn replaces the loading screen.
		ENGINE->windows().clear();
		ENGINE->windows().pushWindow(adventureInt);
	}

	movementController->onBattleStarted();

	waitForAllDialogs();
}

void CPlayerInterface::battleStart(const BattleID & battleID, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide side, bool replayAllowed)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	bool useQuickCombat = settings["adventure"]["quickCombat"].Bool();
	bool forceQuickCombat = settings["adventure"]["forceQuickCombat"].Bool();

	if ((replayAllowed && useQuickCombat) || forceQuickCombat)
	{
		prepareAutoFightingAI(battleID, army1, army2, tile, hero1, hero2, side);
	}

	waitForAllDialogs();

	BATTLE_EVENT_POSSIBLE_RETURN;
}

void CPlayerInterface::battleUnitsChanged(const BattleID & battleID, const std::vector<UnitChanges> & units)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	for(auto & info : units)
	{
		switch(info.operation)
		{
		case UnitChanges::EOperation::RESET_STATE:
			{
				const CStack * stack = cb->getBattle(battleID)->battleGetStackByID(info.id );

				if(!stack)
				{
					logGlobal->error("Invalid unit ID %d", info.id);
					continue;
				}
				battleInt->stackReset(stack);
			}
			break;
		case UnitChanges::EOperation::REMOVE:
			battleInt->stackRemoved(info.id);
			break;
		case UnitChanges::EOperation::ADD:
			{
				const CStack * unit = cb->getBattle(battleID)->battleGetStackByID(info.id);
				if(!unit)
				{
					logGlobal->error("Invalid unit ID %d", info.id);
					continue;
				}
				battleInt->stackAdded(unit);
			}
			break;
		default:
			logGlobal->error("Unknown unit operation %d", (int)info.operation);
			break;
		}
	}
}

void CPlayerInterface::battleObstaclesChanged(const BattleID & battleID, const std::vector<ObstacleChanges> & obstacles)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	std::vector<std::shared_ptr<const CObstacleInstance>> newObstacles;
	std::vector<ObstacleChanges> removedObstacles;

	for(auto & change : obstacles)
	{
		if(change.operation == BattleChanges::EOperation::ADD)
		{
			auto instance = cb->getBattle(battleID)->battleGetObstacleByID(change.id);
			if(instance)
				newObstacles.push_back(instance);
			else
				logNetwork->error("Invalid obstacle instance %d", change.id);
		}
		if(change.operation == BattleChanges::EOperation::REMOVE)
			removedObstacles.push_back(change); //Obstacles are already removed, so, show animation based on json struct
	}

	if (!newObstacles.empty())
		battleInt->obstaclePlaced(newObstacles);

	if (!removedObstacles.empty())
		battleInt->obstacleRemoved(removedObstacles);

	battleInt->fieldController->redrawBackgroundWithHexes();
}

void CPlayerInterface::battleCatapultAttacked(const BattleID & battleID, const CatapultAttack & ca)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->stackIsCatapulting(ca);
}

void CPlayerInterface::battleNewRound(const BattleID & battleID) //called at the beginning of each turn, round=-1 is the tactic phase, round=0 is the first "normal" turn
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->newRound();
}

void CPlayerInterface::actionStarted(const BattleID & battleID, const BattleAction &action)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->startAction(action);
}

void CPlayerInterface::actionFinished(const BattleID & battleID, const BattleAction &action)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->endAction(action);
}

void CPlayerInterface::activeStack(const BattleID & battleID, const CStack * stack) //called when it's turn of that stack
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	logGlobal->trace("Awaiting command for %s", stack->nodeName());

	assert(!cb->getBattle(battleID)->battleIsFinished());
	if (cb->getBattle(battleID)->battleIsFinished())
	{
		logGlobal->error("Received CPlayerInterface::activeStack after battle is finished!");

		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
		return ;
	}

	if (autofightingAI)
	{
		if (isAutoFightOn)
		{
			//FIXME: we want client rendering to proceed while AI is making actions
			// so unlock mutex while AI is busy since this might take quite a while, especially if hero has many spells
			auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			autofightingAI->activeStack(battleID, stack);
			return;
		}
		unregisterBattleInterface(autofightingAI);
	}

	assert(battleInt);
	if(!battleInt)
	{
		// probably battle is finished already
		cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
	}

	battleInt->stackActivated(stack);
}

void CPlayerInterface::battleEnd(const BattleID & battleID, const BattleResult *br, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	const bool isArena = cb->getMapHeader()->battleOnly;
	if(isAutoFightOn || autofightingAI)
	{
		isAutoFightOn = false;
		unregisterBattleInterface(autofightingAI);

		if(!battleInt)
		{
			bool requiresResultSelection = queryID != QueryID::NONE || isAutoFightEndBattle;
			bool allowManualReplay = requiresResultSelection && cb->getStartInfo()->extraOptionsInfo.unlimitedReplay && !isAutoFightEndBattle;

			const int32_t arenaTimeout = isArena ? getArenaBattleResultTimeout(queryID) : -1;
			auto wnd = std::make_shared<BattleResultWindow>(*br, *this, allowManualReplay, isArena ? queryID : QueryID::NONE, arenaTimeout);

			if (requiresResultSelection)
			{
				wnd->resultCallback = [this, queryID](ui32 selection)
				{
					cb->selectionMade(selection, queryID);
				};
			}
			
			isAutoFightEndBattle = false;

			ENGINE->windows().pushWindow(wnd);
			if(isArena)
				return;
			// #1490 - during AI turn when quick combat is on, we need to display the message and wait for user to close it.
			// Otherwise NewTurn causes freeze.
			waitWhileDialog();
			return;
		}
	}

	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->battleFinished(*br, queryID);
}

void CPlayerInterface::battleLogMessage(const BattleID & battleID, const std::vector<MetaString> & lines)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->displayBattleLog(lines);
}

void CPlayerInterface::battleStackMoved(const BattleID & battleID, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->stackMoved(stack, dest, distance, teleport);
}
void CPlayerInterface::battleSpellCast(const BattleID & battleID, const BattleSpellCast * sc)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->spellCast(sc);
}
void CPlayerInterface::battleStacksEffectsSet(const BattleID & battleID, const SetStackEffect & sse)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->battleStacksEffectsSet(sse);
}
void CPlayerInterface::battleTriggerEffect(const BattleID & battleID, const BattleTriggerEffect & bte)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->effectsController->battleTriggerEffect(bte);

	if(bte.effect == BonusType::MANA_DRAIN)
	{
		const CGHeroInstance * manaDrainedHero = GAME->interface()->cb->getHero(ObjectInstanceID(bte.additionalInfo));
		battleInt->windowObject->heroManaPointsChanged(manaDrainedHero);
	}
}
void CPlayerInterface::battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	std::vector<StackAttackedInfo> arg;
	for(auto & elem : bsa)
	{
		const CStack * defender = cb->getBattle(battleID)->battleGetStackByID(elem.stackAttacked, false);
		const CStack * attacker = cb->getBattle(battleID)->battleGetStackByID(elem.attackerID, false);

		assert(defender);

		StackAttackedInfo     info;
		info.defender       = defender;
		info.attacker       = attacker;
		info.damageDealt    = elem.damageAmount;
		info.amountKilled   = elem.killedAmount;
		info.spellEffect    = SpellID::NONE;
		info.indirectAttack = ranged;
		info.killed         = elem.killed();
		info.rebirth        = elem.willRebirth();
		info.cloneKilled    = elem.cloneKilled();
		info.fireShield     = elem.fireShield();

		if (elem.isSpell())
			info.spellEffect = elem.spellID;

		arg.push_back(info);
	}
	battleInt->stacksAreAttacked(arg);
}
void CPlayerInterface::battleAttack(const BattleID & battleID, const BattleAttack * ba)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	StackAttackInfo info;
	info.attacker = cb->getBattle(battleID)->battleGetStackByID(ba->stackAttacking);
	info.defender = nullptr;
	info.indirectAttack = ba->shot();
	info.lucky = ba->lucky();
	info.unlucky = ba->unlucky();
	info.deathBlow = ba->deathBlow();
	info.lifeDrain = ba->lifeDrain();
	info.playCustomAnimation = ba->playCustomAnimation();
	info.tile = ba->tile;
	info.spellEffect = SpellID::NONE;

	if (ba->spellLike())
		info.spellEffect = ba->spellID;

	for(auto & elem : ba->bsa)
	{
		if(!elem.isSecondary())
		{
			assert(info.defender == nullptr);
			info.defender = cb->getBattle(battleID)->battleGetStackByID(elem.stackAttacked);
		}
		else
		{
			info.secondaryDefender.push_back(cb->getBattle(battleID)->battleGetStackByID(elem.stackAttacked));
		}
	}
	assert(info.defender != nullptr || (info.spellEffect != SpellID::NONE && info.indirectAttack));
	assert(info.attacker != nullptr);

	battleInt->stackAttacking(info);
}

void CPlayerInterface::battleGateStateChanged(const BattleID & battleID, const EGateState state)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->gateStateChanged(state);
}

void CPlayerInterface::yourTacticPhase(const BattleID & battleID, int distance)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
}

void CPlayerInterface::showInfoDialog(EInfoWindowMode type, const std::string &text, const std::vector<Component> & components, int soundID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	bool autoTryHover = settings["gameTweaks"]["infoBarPick"].Bool() && type == EInfoWindowMode::AUTO;
	auto timer = type == EInfoWindowMode::INFO ? 3000 : 4500; //Implement long info windows like in HD mod

	if(autoTryHover || type == EInfoWindowMode::INFO)
	{
		waitWhileDialog(); //Fix for mantis #98
		adventureInt->showInfoBoxMessage(components, text, timer);

		// abort movement, if any. Strictly speaking unnecessary, but prevents some edge cases, like movement sound on visiting Magi Hut with "show messages in status window" on
		movementController->requestMovementAbort();

		if (makingTurn && ENGINE->windows().count() > 0 && GAME->interface() == this)
			ENGINE->sound().playSound(static_cast<soundBase::soundID>(soundID));
		return;
	}

	if (settings["session"]["autoSkip"].Bool() && !ENGINE->isKeyboardShiftDown())
	{
		return;
	}
	std::vector<Component> vect = components; //I do not know currently how to avoid copy here
	do
	{
		std::vector<Component> sender = {vect.begin(), vect.begin() + std::min(vect.size(), static_cast<size_t>(8))};
		std::vector<std::shared_ptr<CComponent>> intComps;
		for (auto & component : sender)
			intComps.push_back(std::make_shared<CComponent>(component));
		showInfoDialog(text,intComps,soundID);
		vect.erase(vect.begin(), vect.begin() + std::min(vect.size(), static_cast<size_t>(8)));
	}
	while(!vect.empty());
}

void CPlayerInterface::showInfoDialog(const std::string & text, std::shared_ptr<CComponent> component)
{
	std::vector<std::shared_ptr<CComponent>> intComps;
	intComps.push_back(component);

	showInfoDialog(text, intComps, soundBase::sound_todo);
}

void CPlayerInterface::showInfoDialog(const std::string &text, const std::vector<std::shared_ptr<CComponent>> & components, int soundID)
{
	LOG_TRACE_PARAMS(logGlobal, "player=%s, text=%s, is GAME->interface()=%d", playerID % text % (this==GAME->interface()));
	waitWhileDialog();

	if (settings["session"]["autoSkip"].Bool() && !ENGINE->isKeyboardShiftDown())
	{
		return;
	}
	std::shared_ptr<CInfoWindow> temp = CInfoWindow::create(text, playerID, components);

	if ((makingTurn || (battleInt && battleInt->curInt && battleInt->curInt.get() == this)) && ENGINE->windows().count() > 0 && GAME->interface() == this)
	{
		ENGINE->sound().playSound(static_cast<soundBase::soundID>(soundID));
		showingDialog->setBusy();
		movementController->requestMovementAbort(); // interrupt movement to show dialog
		ENGINE->windows().pushWindow(temp);
	}
	else
	{
		dialogs.push_back(temp);
	}
}

void CPlayerInterface::showInfoDialogAndWait(std::vector<Component> & components, const MetaString & text)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	std::string str = text.toString();

	showInfoDialog(EInfoWindowMode::MODAL, str, components, 0);
	waitWhileDialog();
}

void CPlayerInterface::showYesNoDialog(const std::string &text, CFunctionList<void()> onYes, CFunctionList<void()> onNo, const std::vector<std::shared_ptr<CComponent>> & components)
{
	waitWhileDialog();
	movementController->requestMovementAbort();
	GAME->interface()->showingDialog->setBusy();
	CInfoWindow::showYesNoDialog(text, components, onYes, onNo, playerID);
}

void CPlayerInterface::showBlockingDialog(const std::string &text, const std::vector<Component> &components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();

	movementController->requestMovementAbort();
	ENGINE->sound().playSound(static_cast<soundBase::soundID>(soundID));

	if (!selection && cancel) //simple yes/no dialog
	{
		if(settings["general"]["enableUiEnhancements"].Bool() && safeToAutoaccept)
		{
			cb->selectionMade(1, askID); //as in HD mod, we try to skip dialogs that server considers visual fluff which does not affect gamestate
			return;
		}

		std::vector<std::shared_ptr<CComponent>> intComps;
		for (auto & component : components)
			intComps.push_back(std::make_shared<CComponent>(component)); //will be deleted by close in window

		showYesNoDialog(text, [this, askID](){ cb->selectionMade(1, askID); }, [this, askID](){ cb->selectionMade(0, askID); }, intComps);
	}
	else if (selection)
	{
		std::vector<std::shared_ptr<CSelectableComponent>> intComps;
		for (auto & component : components)
			intComps.push_back(std::make_shared<CSelectableComponent>(component)); //will be deleted by CSelWindow::close

		std::vector<std::pair<AnimationPath,CFunctionList<void()> > > pom;
		pom.push_back({ AnimationPath::builtin("IOKAY.DEF"),0});
		if (cancel)
		{
			pom.push_back({AnimationPath::builtin("ICANCEL.DEF"),0});
		}

		int charperline = 35;
		if (pom.size() > 1)
			charperline = 50;
		ENGINE->windows().createAndPushWindow<CSelWindow>(text, playerID, charperline, intComps, pom, askID);
		intComps[0]->clickPressed(ENGINE->getCursorPosition());
		intComps[0]->clickReleased(ENGINE->getCursorPosition());
	}
}

void CPlayerInterface::showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	movementController->showTeleportDialog(hero, channel, exits, impassable, askID);
}

void CPlayerInterface::showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	std::vector<ObjectInstanceID> objectGuiOrdered = objects;

	std::map<ObjectInstanceID, int> townOrder;
	auto ownedTowns = localState->getOwnedTowns();

	for (int i = 0; i < ownedTowns.size(); ++i)
		townOrder[ownedTowns[i]->id] = i;

	auto townComparator = [&townOrder](const ObjectInstanceID & left, const ObjectInstanceID & right){
		uint32_t leftIndex= townOrder.count(left) ? townOrder.at(left) : std::numeric_limits<uint32_t>::max();
		uint32_t rightIndex = townOrder.count(right) ? townOrder.at(right) : std::numeric_limits<uint32_t>::max();
		return leftIndex < rightIndex;
	};
	std::stable_sort(objectGuiOrdered.begin(), objectGuiOrdered.end(), townComparator);

	const std::string localTitle = title.toString();
	const std::string localDescription = description.toString();

	std::vector<int> tempList;
	tempList.reserve(objectGuiOrdered.size());

	for(const auto & item : objectGuiOrdered)
		tempList.push_back(item.getNum());

	CComponent localIconC(icon);

	std::shared_ptr<CIntObject> localIcon = localIconC.image;
	localIconC.removeChild(localIcon.get(), false);

	std::vector<std::shared_ptr<IImage>> images;
	for(const auto & obj : objectGuiOrdered)
	{
		if(!settings["general"]["enableUiEnhancements"].Bool())
			break;
		const CGTownInstance * t = dynamic_cast<const CGTownInstance *>(cb->getObj(obj));
		if(t)
		{
			auto image = ENGINE->renderHandler().loadImage(AnimationPath::builtin("ITPA"), t->getTown()->clientInfo.icons[t->hasFort()][false] + 2, 0, EImageBlitMode::OPAQUE);
			image->scaleTo(Point(35, 23), EScalingAlgorithm::NEAREST);
			images.push_back(image);
		}
	}

	auto selectCallback = [this, askID, objectGuiOrdered](int selection)
	{
		cb->sendQueryReply(objectGuiOrdered[selection], askID);
	};

	auto cancelCallback = [this, askID]()
	{
		cb->sendQueryReply(std::nullopt, askID);
	};

	auto wnd = std::make_shared<CObjectListWindow>(tempList, localIcon, localTitle, localDescription, selectCallback, 0, images);
	wnd->onExit = cancelCallback;
	wnd->onPopup = [this, objectGuiOrdered](int index) { CRClickPopup::createAndPush(cb->getObj(objectGuiOrdered[index]), ENGINE->getCursorPosition()); };
	wnd->onClicked = [this, objectGuiOrdered](int index) { adventureInt->centerOnObject(cb->getObj(objectGuiOrdered[index])); ENGINE->windows().totalRedraw(); };
	ENGINE->windows().pushWindow(wnd);
}

void CPlayerInterface::tileRevealed(const FowTilesType &pos)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	//FIXME: wait for dialog? Magi hut/eye would benefit from this but may break other areas
	adventureInt->onMapTilesChanged(pos);
}

void CPlayerInterface::tileHidden(const FowTilesType &pos)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onMapTilesChanged(pos);
}

void CPlayerInterface::openHeroWindow(const CGHeroInstance *hero)
{
	ENGINE->windows().createAndPushWindow<CHeroWindow>(hero);
}

void CPlayerInterface::availableCreaturesChanged( const CGDwelling *town )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (const CGTownInstance * townObj = dynamic_cast<const CGTownInstance*>(town))
	{
		for (auto fortScreen : ENGINE->windows().findWindows<CFortScreen>())
			fortScreen->creaturesChangedEventHandler();

		for (auto castleInterface : ENGINE->windows().findWindows<CCastleInterface>())
			if(castleInterface->town == town)
				castleInterface->creaturesChangedEventHandler();

		if (townObj)
			for (auto ki : ENGINE->windows().findWindows<CKingdomInterface>())
				ki->townChanged(townObj);
	}
	else if(town && ENGINE->windows().count() > 0 && (town->ID == Obj::CREATURE_GENERATOR1
		||  town->ID == Obj::CREATURE_GENERATOR4  ||  town->ID == Obj::WAR_MACHINE_FACTORY))
	{
		for (auto crw : ENGINE->windows().findWindows<CRecruitmentWindow>())
			if (crw->dwelling == town)
				crw->availableCreaturesChanged();
	}
}

void CPlayerInterface::heroBonusChanged( const CGHeroInstance *hero, const Bonus &bonus, bool gain )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (bonus.type == BonusType::NONE)
		return;

	adventureInt->onHeroChanged(hero);

	//recalculate paths because hero has lost or gained bonus influencing pathfinding
	if (bonus.type == BonusType::FLYING_MOVEMENT || bonus.type == BonusType::WATER_WALKING || bonus.type == BonusType::ROUGH_TERRAIN_DISCOUNT || bonus.type == BonusType::NO_TERRAIN_PENALTY)
		localState->verifyPath(hero);
}

void CPlayerInterface::moveHero( const CGHeroInstance *h, const CGPath& path )
{
	LOG_TRACE(logGlobal);
	if (!GAME->interface()->makingTurn)
		return;
	if(arenaDeploymentActive.load())
	{
		// Deployment permits ending the turn only. Do not start movement animation
		// for requests that the server must reject as well.
		logGlobal->debug("[ARENA] ignoring hero movement during deployment");
		return;
	}

	assert(h);
	assert(!showingDialog->isBusy());
	assert(dialogs.empty());

	if (!h)
		return; //can't find hero

	//It shouldn't be possible to move hero with open dialog (or dialog waiting in bg)
	if (showingDialog->isBusy() || !dialogs.empty())
		return;

	if (localState->isHeroSleeping(h))
		localState->setHeroAwaken(h);

	movementController->requestMovementStart(h, path);
}

void CPlayerInterface::showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	auto onEnd = [this, queryID](){ cb->selectionMade(0, queryID); };

	if (movementController->isHeroMovingThroughGarrison(down, up))
	{
		onEnd();
		return;
	}

	waitForAllDialogs();

	auto cgw = std::make_shared<CGarrisonWindow>(up, down, removableUnits, customTitle);
	cgw->quit->addCallback(onEnd);
	ENGINE->windows().pushWindow(cgw);
}

void CPlayerInterface::requestRealized( PackageApplied *pa )
{
	if(pa->packType == CTypeList::getInstance().getTypeID<MoveHero>(nullptr))
		movementController->onMoveHeroApplied();

	if(pa->packType == CTypeList::getInstance().getTypeID<QueryReply>(nullptr))
		movementController->onQueryReplyApplied();
}

void CPlayerInterface::showHeroExchange(ObjectInstanceID hero1, ObjectInstanceID hero2)
{
	heroExchangeStarted(hero1, hero2, QueryID(-1));
}

void CPlayerInterface::heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID query)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	ENGINE->windows().createAndPushWindow<CExchangeWindow>(hero1, hero2, query);
}

void CPlayerInterface::beforeObjectPropertyChanged(const SetObjectProperty * sop)
{
	if (sop->what == ObjProperty::OWNER)
	{
		const CGObjectInstance * obj = cb->getObj(sop->id);

		if(obj->ID == Obj::TOWN)
		{
			auto town = static_cast<const CGTownInstance *>(obj);

			if(obj->tempOwner == playerID)
			{
				localState->removeOwnedTown(town);
				adventureInt->onTownChanged(town);
			}
		}
	}
}

void CPlayerInterface::objectPropertyChanged(const SetObjectProperty * sop)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	if (sop->what == ObjProperty::OWNER)
	{
		const CGObjectInstance * obj = cb->getObj(sop->id);

		if(obj->ID == Obj::TOWN)
		{
			auto town = static_cast<const CGTownInstance *>(obj);

			if(obj->tempOwner == playerID)
			{
				localState->addOwnedTown(town);
				adventureInt->onTownChanged(town);
			}
		}

		//redraw minimap if owner changed
		std::set<int3> pos = obj->getBlockedPos();
		FowTilesType upos(pos.begin(), pos.end());
		adventureInt->onMapTilesChanged(upos);

		assert(cb->getTownsInfo().size() == localState->getOwnedTowns().size());
	}
}

void CPlayerInterface::initializeHeroTownList()
{
	if(localState->getWanderingHeroes().empty())
	{
		for(auto & hero : cb->getHeroesInfo())
		{
			if(!hero->isGarrisoned())
				localState->addWanderingHero(hero);
		}
	}

	if(localState->getOwnedTowns().empty())
	{
		for(auto & town : cb->getTownsInfo())
			localState->addOwnedTown(town);
	}

	localState->deserialize(*cb->getPlayerState(playerID)->playerLocalSettings);

	if(adventureInt)
		adventureInt->onHeroChanged(nullptr);
}

void CPlayerInterface::showRecruitmentDialog(const CGDwelling *dwelling, const CArmedInstance *dst, int level, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	auto recruitCb = [this, dwelling, dst](CreatureID id, int count)
	{
		cb->recruitCreatures(dwelling, dst, id, count, -1);
	};
	auto closeCb = [this, queryID]()
	{
		cb->selectionMade(0, queryID);
	};
	ENGINE->windows().createAndPushWindow<CRecruitmentWindow>(dwelling, level, dst, recruitCb, closeCb);
}

void CPlayerInterface::waitWhileDialog()
{
	if (ENGINE->amIGuiThread())
	{
		// GUI event handlers (for example hero dismissal) already run on the
		// thread that owns the dialog. Waiting here is neither needed nor legal;
		// returning is the expected asynchronous path, not a runtime failure.
		logGlobal->debug("Skipping synchronous dialog wait in GUI thread");
		return;
	}

	auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
	showingDialog->waitWhileBusy();
}

void CPlayerInterface::showShipyardDialog(const IShipyard *obj)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	auto state = obj->shipyardStatus();
	TResources cost;
	obj->getBoatCost(cost);
	ENGINE->windows().createAndPushWindow<CShipyardWindow>(cost, state, obj->getBoatType(), [this, obj](){ cb->buildBoat(obj); });
}

void CPlayerInterface::newObject( const CGObjectInstance * obj )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	//we might have built a boat in shipyard in opened town screen
	if (obj->ID == Obj::BOAT
		&& GAME->interface()->castleInt
		&&  obj->visitablePos() == GAME->interface()->castleInt->town->bestLocation())
	{
		GAME->interface()->castleInt->addBuilding(BuildingID::SHIP);
	}
}

void CPlayerInterface::centerView (int3 pos, int focusTime)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	ENGINE->cursor().hide();
	adventureInt->centerOnTile(pos);
	if (focusTime)
	{
		ENGINE->windows().totalRedraw();
		{
			IgnoreEvents ignore(*this);
			auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			std::this_thread::sleep_for(std::chrono::milliseconds(focusTime));
		}
	}
	ENGINE->cursor().show();
}

void CPlayerInterface::objectRemoved(const CGObjectInstance * obj, const PlayerColor & initiator)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(playerID == initiator)
	{
		auto removalSound = obj->getRemovalSound(CRandomGenerator::getDefault());
		if (removalSound)
		{
			waitWhileDialog();
			ENGINE->sound().playSound(removalSound.value());
		}
	}
	GAME->map().waitForOngoingAnimations();

	if(obj->ID == Obj::HERO && obj->tempOwner == playerID)
	{
		const CGHeroInstance * h = static_cast<const CGHeroInstance *>(obj);
		heroKilled(h);
	}

	if(obj->ID == Obj::TOWN && obj->tempOwner == playerID)
	{
		const CGTownInstance * t = static_cast<const CGTownInstance *>(obj);
		townRemoved(t);
	}
	ENGINE->fakeMouseMove();
}

void CPlayerInterface::objectRemovedAfter()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onMapTilesChanged(boost::none);

	// visiting or garrisoned hero removed - update window
	if (castleInt)
		castleInt->updateGarrisons();

	for (auto ki : ENGINE->windows().findWindows<CKingdomInterface>())
		ki->heroRemoved();
}

void CPlayerInterface::playerBlocked(int reason, bool start)
{
	if(reason == PlayerBlocked::EReason::UPCOMING_BATTLE)
	{
		if(GAME->server().howManyPlayerInterfaces() > 1 && GAME->interface() != this && GAME->interface()->makingTurn == false && !GAME->map().getMap()->battleOnly)
		{
			//one of our players who isn't last in order got attacked not by our another player (happens for example in hotseat mode)
			GAME->setInterfaceInstance(this);
			adventureInt->onCurrentPlayerChanged(playerID);
			std::string msg = LIBRARY->generaltexth->translate("vcmi.adventureMap.playerAttacked");
			boost::replace_first(msg, "%s", cb->getStartInfo()->playerInfos.find(playerID)->second.name);
			std::vector<std::shared_ptr<CComponent>> cmp;
			cmp.push_back(std::make_shared<CComponent>(ComponentType::FLAG, playerID));
			makingTurn = true; //workaround for stiff showInfoDialog implementation
			showInfoDialog(msg, cmp);
			waitWhileDialog();
			makingTurn = false;
		}
	}
}

void CPlayerInterface::update()
{
	//if there are any waiting dialogs, show them
	if (makingTurn && !dialogs.empty() && !showingDialog->isBusy())
	{
		showingDialog->setBusy();
		ENGINE->windows().pushWindow(dialogs.front());
		dialogs.pop_front();
	}
}

void CPlayerInterface::endNetwork()
{
	showingDialog->requestTermination();
}

int CPlayerInterface::getLastIndex( std::string namePrefix)
{
	using namespace boost::filesystem;
	using namespace boost::algorithm;

	path gamesDir = VCMIDirs::get().userSavePath();
	std::map<std::time_t, int> dates; //save number => datestamp

	const directory_iterator enddir;
	if (!exists(gamesDir))
		create_directory(gamesDir);
	else
	for (directory_iterator dir(gamesDir); dir != enddir; ++dir)
	{
		if (is_regular_file(dir->status()))
		{
			std::string name = dir->path().filename().string();
			if (starts_with(name, namePrefix) && ends_with(name, ".vcgm1"))
			{
				char nr = name[namePrefix.size()];
				if (std::isdigit(nr))
					dates[last_write_time(dir->path())] = boost::lexical_cast<int>(nr);
			}
		}
	}

	if (!dates.empty())
		return (--dates.end())->second; //return latest file number
	return 0;
}

void CPlayerInterface::gameOver(PlayerColor player, const EVictoryLossCheckResult & victoryLossCheckResult )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	if (player == playerID)
	{
		if (victoryLossCheckResult.loss())
			showInfoDialog(LIBRARY->generaltexth->allTexts[95]);

		auto previousInterface = GAME->interface(); //without multiple player interfaces some of lines below are useless, but for hotseat we wanna swap player interface temporarily

		GAME->setInterfaceInstance(this); //this is needed for dialog to show and avoid freeze, dialog showing logic should be reworked someday

		if(!makingTurn)
		{
			makingTurn = true; //also needed for dialog to show with current implementation
			waitForAllDialogs();
			makingTurn = false;
		}
		else
			waitForAllDialogs();

		GAME->setInterfaceInstance(previousInterface);
	}
}

void CPlayerInterface::playerBonusChanged( const Bonus &bonus, bool gain )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
}

void CPlayerInterface::showPuzzleMap()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();

	//TODO: interface should not know the real position of Grail...
	double ratio = 0;
	int3 grailPos = cb->getGrailPos(&ratio);

	ENGINE->windows().createAndPushWindow<CPuzzleWindow>(grailPos, ratio);
}

void CPlayerInterface::viewWorldMap()
{
	adventureInt->openWorldView();
}

void CPlayerInterface::advmapSpellCast(const CGHeroInstance * caster, SpellID spellID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	if(ENGINE->windows().topWindow<CSpellWindow>())
		ENGINE->windows().popWindows(1);

	auto castSoundPath = spellID.toSpell()->getCastSound();
	if(!castSoundPath.empty())
		ENGINE->sound().playSound(castSoundPath);
}

void CPlayerInterface::tryDigging(const CGHeroInstance * h)
{
	int msgToShow = -1;

	const auto diggingStatus = h->diggingStatus();

	switch(diggingStatus)
	{
	case EDiggingStatus::CAN_DIG:
		break;
	case EDiggingStatus::LACK_OF_MOVEMENT:
		msgToShow = 56; //"Digging for artifacts requires a whole day, try again tomorrow."
		break;
	case EDiggingStatus::TILE_OCCUPIED:
		msgToShow = 97; //Try searching on clear ground.
		break;
	case EDiggingStatus::WRONG_TERRAIN:
		msgToShow = 60; ////Try looking on land!
		break;
	case EDiggingStatus::BACKPACK_IS_FULL:
		msgToShow = 247; //Searching for the Grail is fruitless...
		break;
	default:
		assert(0);
	}

	if(msgToShow < 0)
		cb->dig(h);
	else
		showInfoDialog(LIBRARY->generaltexth->allTexts[msgToShow]);
}

void CPlayerInterface::battleNewRoundFirst(const BattleID & battleID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BATTLE_EVENT_POSSIBLE_RETURN;

	battleInt->newRoundFirst();
}

void CPlayerInterface::showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	auto onWindowClosed = [this, queryID](){
		cb->selectionMade(0, queryID);
	};

	if(market->allowsTrade(EMarketMode::ARTIFACT_EXP) && visitor->getAlignment() != EAlignment::EVIL)
		ENGINE->windows().createAndPushWindow<CMarketWindow>(market, visitor, onWindowClosed, EMarketMode::ARTIFACT_EXP);
	else if(market->allowsTrade(EMarketMode::CREATURE_EXP) && visitor->getAlignment() != EAlignment::GOOD)
		ENGINE->windows().createAndPushWindow<CMarketWindow>(market, visitor, onWindowClosed, EMarketMode::CREATURE_EXP);
	else if(market->allowsTrade(EMarketMode::CREATURE_UNDEAD))
		ENGINE->windows().createAndPushWindow<CTransformerWindow>(market, visitor, onWindowClosed);
	else if (!market->availableModes().empty())
		for(auto mode = EMarketMode::RESOURCE_RESOURCE; mode != EMarketMode::MARKET_AFTER_LAST_PLACEHOLDER; mode = vstd::next(mode, 1))
		{
			if(vstd::contains(market->availableModes(), mode))
			{
				ENGINE->windows().createAndPushWindow<CMarketWindow>(market, visitor, onWindowClosed, mode);
				break;
			}
		}
	else
		onWindowClosed();
}

void CPlayerInterface::showUniversityWindow(const IMarket *market, const CGHeroInstance *visitor, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	auto onWindowClosed = [this, queryID](){
		cb->selectionMade(0, queryID);
	};
	ENGINE->windows().createAndPushWindow<CUniversityWindow>(visitor, BuildingID::NONE, market, onWindowClosed);
}

void CPlayerInterface::showHillFortWindow(const CGObjectInstance *object, const CGHeroInstance *visitor)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	ENGINE->windows().createAndPushWindow<CHillFortWindow>(visitor, object);
}

void CPlayerInterface::availableArtifactsChanged(const CGBlackMarket * bm)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	for (auto cmw : ENGINE->windows().findWindows<IMarketHolder>())
		cmw->updateArtifacts();
}

void CPlayerInterface::showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QueryID queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	auto onWindowClosed = [this, queryID](){
		if (queryID != QueryID::NONE)
			cb->selectionMade(0, queryID);
	};
	ENGINE->windows().createAndPushWindow<CTavernWindow>(object, onWindowClosed);
}

void CPlayerInterface::showThievesGuildWindow (const CGObjectInstance * obj)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(cb->getMapHeader()->battleOnly)
		return;
	ENGINE->windows().createAndPushWindow<CThievesGuildWindow>(obj);
}

void CPlayerInterface::showQuestLog()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	ENGINE->windows().createAndPushWindow<CQuestLog>(GAME->interface()->cb->getMyQuests());
}

void CPlayerInterface::showShipyardDialogOrProblemPopup(const IShipyard *obj)
{
	if (obj->shipyardStatus() != IBoatGenerator::GOOD)
	{
		MetaString txt;
		obj->getProblemText(txt);
		showInfoDialog(txt.toString());
	}
	else
		showShipyardDialog(obj);
}

void CPlayerInterface::askToAssembleArtifact(const ArtifactLocation &al)
{
	artifactController->askToAssemble(al, true, true);
}

void CPlayerInterface::artifactPut(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(cb->getHero(al.artHolder));
	garrisonsChanged(al.artHolder, ObjectInstanceID());
}

void CPlayerInterface::artifactRemoved(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(cb->getHero(al.artHolder));
	garrisonsChanged(al.artHolder, ObjectInstanceID());
	artifactController->artifactRemoved();
}

void CPlayerInterface::artifactMoved(const ArtifactLocation &src, const ArtifactLocation &dst)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(cb->getHero(dst.artHolder));
	garrisonsChanged(src.artHolder, dst.artHolder);
	artifactController->artifactMoved();
}

void CPlayerInterface::bulkArtMovementStart(size_t totalNumOfArts, size_t possibleAssemblyNumOfArts)
{
	artifactController->bulkArtMovementStart(totalNumOfArts, possibleAssemblyNumOfArts);
}

void CPlayerInterface::artifactAssembled(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(cb->getHero(al.artHolder));
	artifactController->artifactAssembled();
}

void CPlayerInterface::artifactDisassembled(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->onHeroChanged(cb->getHero(al.artHolder));
	artifactController->artifactDisassembled();
}

void CPlayerInterface::waitForAllDialogs()
{
	if (!makingTurn)
		return;

	while(!dialogs.empty())
	{
		auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	waitWhileDialog();
}

void CPlayerInterface::proposeLoadingGame()
{
	showYesNoDialog(
		LIBRARY->generaltexth->allTexts[68],
		[]()
		{
			GAME->server().endGameplay();
			GAME->mainmenu()->menu->switchToTab("load");
		},
		nullptr
	);
}

void CPlayerInterface::quickSaveGame()
{
	// notify player about saving
	MetaString txt;
	txt.appendTextID("vcmi.adventureMap.savingQuickSave");
	txt.replaceRawString(QUICKSAVE_PATH);
	GAME->server().getGameChat().sendMessageGameplay(txt.toString());
	GAME->interface()->cb->save(QUICKSAVE_PATH, false);
	hasQuickSave = true;
	if(adventureInt)
		adventureInt->updateActiveState();
}

bool CPlayerInterface::checkQuickLoadingGame(bool verbose)
{
	if(!CResourceHandler::get("local")->existsResource(ResourcePath(QUICKSAVE_PATH, EResType::SAVEGAME)))
	{
		if(verbose)
			logGlobal->error("No quicksave file found at %s", QUICKSAVE_PATH);
		else
			logGlobal->trace("No quicksave file found at %s", QUICKSAVE_PATH);
		hasQuickSave = false;
		if(cb && adventureInt)
			adventureInt->updateActiveState();
		return false;
	}
	auto error = GAME->server().canQuickLoadGame(QUICKSAVE_PATH);
	if(error)
	{
		if(verbose)
			logGlobal->error("Cannot quick load game at %s: %s", QUICKSAVE_PATH, *error);
		else
			logGlobal->trace("Cannot quick load game at %s: %s", QUICKSAVE_PATH, *error);
		hasQuickSave = false;
		if(cb && adventureInt)
			adventureInt->updateActiveState();
		return false;
	}
	return true;
}

void CPlayerInterface::proposeQuickLoadingGame()
{
	if(!checkQuickLoadingGame(true))
		return;

	auto onYes = [this]() -> void
	{
		GAME->server().quickLoadGame(QUICKSAVE_PATH);
	};

	GAME->interface()->showYesNoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.confirmQuickLoadGame"), onYes, nullptr);
}

bool CPlayerInterface::capturedAllEvents()
{
	if(movementController->isHeroMoving())
	{
		//just inform that we are capturing events. they will be processed by heroMoved() in client thread.
		return true;
	}

	bool needToLockAdventureMap = adventureInt && adventureInt->isActive() && GAME->map().hasOngoingAnimations();
	bool quickCombatOngoing = isAutoFightOn && !battleInt;

	if (ignoreEvents || needToLockAdventureMap || quickCombatOngoing )
	{
		ENGINE->input().ignoreEventsUntilInput();
		return true;
	}

	return false;
}

void CPlayerInterface::prepareAutoFightingAI(const BattleID &bid, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide side)
{
	std::string combatAI = settings["ai"]["combatAlliedAI"].String();
	if(cb->getMapHeader()->battleOnly)
	{
		const auto & serverCombatAI = cb->getSettings().getValue(EGameSettings::ARENA_COMBAT_AI);
		if(!serverCombatAI.isString() || !serverCombatAI.String().empty())
		{
			if(!serverCombatAI.isString() || (serverCombatAI.String() != "BattleAI" && serverCombatAI.String() != "MMAI"))
				throw std::runtime_error("Invalid server arena combat AI");
			combatAI = serverCombatAI.String();
		}
	}
	logGlobal->info("[ARENA] autocombat AI: %s", combatAI);
	autofightingAI = CDynLibHandler::getNewBattleAI(combatAI);

	AutocombatPreferences autocombatPreferences = AutocombatPreferences();
	autocombatPreferences.enableSpellsUsage = settings["battle"]["enableAutocombatSpells"].Bool();
	autocombatPreferences.losslessFastResolve = isAutoFightEndBattle;
	if(const char * probeOverride = std::getenv("VCMI_ARENA_BATTLE_PROBE_ENABLE_SPELLS"))
	{
		const std::string value(probeOverride);
		if(value == "0")
			autocombatPreferences.enableSpellsUsage = false;
		else if(value == "1")
			autocombatPreferences.enableSpellsUsage = true;
		else
			throw std::runtime_error("VCMI_ARENA_BATTLE_PROBE_ENABLE_SPELLS must be '0' or '1'");
		logGlobal->info("[ARENA-PROBE] autocombat spells override: %s",
			autocombatPreferences.enableSpellsUsage ? "enabled" : "disabled");
	}

	autofightingAI->initBattleInterface(env, cb, autocombatPreferences);
	autofightingAI->battleStart(bid, army1, army2, tile, hero1, hero2, side, false);
	isAutoFightOn = true;
	registerBattleInterface(autofightingAI);
}

void CPlayerInterface::showWorldViewEx(const std::vector<ObjectPosInfo>& objectPositions, bool showTerrain)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	adventureInt->openWorldView(objectPositions, showTerrain );
}

void CPlayerInterface::setColorScheme(ColorScheme scheme)
{
	ENGINE->screenHandler().setColorScheme(scheme);
}

std::optional<BattleAction> CPlayerInterface::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	return std::nullopt;
}

void CPlayerInterface::registerBattleInterface(std::shared_ptr<CBattleGameInterface> battleEvents)
{
	autofightingAI = battleEvents;
	GAME->server().client->registerBattleInterface(battleEvents, playerID);
}

void CPlayerInterface::unregisterBattleInterface(std::shared_ptr<CBattleGameInterface> battleEvents)
{
	assert(battleEvents == autofightingAI);
	GAME->server().client->unregisterBattleInterface(autofightingAI, playerID);
	autofightingAI.reset();
}

void CPlayerInterface::responseStatistic(StatisticDataSet & statistic)
{
	ENGINE->windows().createAndPushWindow<CStatisticScreen>(statistic);
}
