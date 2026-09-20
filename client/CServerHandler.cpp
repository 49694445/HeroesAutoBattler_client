/*
 * CServerHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CServerHandler.h"

#include "CPlayerInterface.h"
#include "Client.h"
#include "ClientNetPackVisitors.h"
#include "GameChatHandler.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "LobbyClientNetPackVisitors.h"
#include "Discord.h"

#include "globalLobby/GlobalLobbyClient.h"

#include "gui/WindowHandler.h"

#include "lobby/CSelectionBase.h"
#include "lobby/CLobbyScreen.h"
#include "lobby/CBonusSelection.h"

#include "media/CMusicHandler.h"
#include "media/IVideoPlayer.h"

#include "mainmenu/CMainMenu.h"
#include "mainmenu/CPrologEpilogVideo.h"
#include "mainmenu/CHighScoreScreen.h"

#include "windows/InfoWindows.h"
#include "windows/GUIClasses.h"

#include "../lib/CConfigHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/ConditionalWait.h"
#include "../lib/CThreadHelper.h"
#include "../lib/StartInfo.h"
#include "../lib/TurnTimerInfo.h"
#include "../lib/VCMIDirs.h"
#include "../lib/campaign/CampaignState.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/gameState/HighScore.h"
#include "../lib/CPlayerState.h"
#include "../lib/mapping/CMap.h"
#include "../lib/mapping/CMapInfo.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/MiscObjects.h"
#include "../lib/modding/ModIncompatibility.h"
#include "../lib/rmg/CMapGenOptions.h"
#include "../lib/serializer/GameConnection.h"
#include "../lib/UnlockGuard.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <vcmi/events/EventBus.h>
#include <SDL_thread.h>

#include <boost/lexical_cast.hpp>

namespace
{
	std::mutex headlessInterfaceMutex;

	std::mutex & getInterfaceMutex()
	{
		return ENGINE ? ENGINE->interfaceMutex : headlessInterfaceMutex;
	}
}

CServerHandler::~CServerHandler()
{
	networkHandler->stop();

	if (threadNetwork.joinable())
		threadNetwork.join();
}

void CServerHandler::endNetwork()
{
	if (client)
		client->endNetwork();
	networkHandler->stop();

	if (threadNetwork.joinable())
		threadNetwork.join();
}

CServerHandler::CServerHandler()
	: networkHandler(INetworkHandler::createHandler())
	, lobbyClient(std::make_unique<GlobalLobbyClient>())
	, gameChat(std::make_unique<GameChatHandler>())
	, threadNetwork(&CServerHandler::threadRunNetwork, this)
	, state(EClientState::NONE)
	, redirectRetryTimer(*this, ETimerPurpose::REDIRECT_RETRY)
	, authenticationTimeoutTimer(*this, ETimerPurpose::AUTHENTICATION_TIMEOUT)
	, serverPort(0)
	, campaignStateToSend(nullptr)
	, screenType(ESelectionScreen::unknown)
	, serverMode(EServerMode::NONE)
	, loadMode(ELoadMode::NONE)
	, hotseatMode(false)
	, battleMode(false)
	, client(nullptr)
{
	uuid = boost::uuids::to_string(boost::uuids::random_generator()());
}

CServerHandler::TimerListener::TimerListener(CServerHandler & owner, ETimerPurpose purpose)
	: owner(owner)
	, purpose(purpose)
{
}

void CServerHandler::TimerListener::onTimer()
{
	switch(purpose)
	{
	case ETimerPurpose::REDIRECT_RETRY:
		owner.onRedirectRetryTimer();
		break;
	case ETimerPurpose::AUTHENTICATION_TIMEOUT:
		owner.onAuthenticationTimeoutTimer();
		break;
	}
}

void CServerHandler::threadRunNetwork()
{
	setThreadName("runNetwork");
	logGlobal->info("Starting network thread");
	try {
		networkHandler->run();
	}
	catch (const TerminationRequestedException &)
	{
		// VCMI can run SDL methods on network thread, leading to usage of thread-local storage by SDL
		// Such storage needs to be cleaned up manually for threads that were not created by SDL
		SDL_TLSCleanup();
		logGlobal->info("Terminating network thread");
		return;
	}
	SDL_TLSCleanup();
	logGlobal->info("Ending network thread");
}

void CServerHandler::resetStateForLobby(EStartMode mode, ESelectionScreen screen, EServerMode newServerMode, const std::vector<std::string> & playerNames)
{
	logicConnection.reset();
	prepareStateForLobby(mode, screen, newServerMode, playerNames);
}

void CServerHandler::prepareStateForLobby(EStartMode mode, ESelectionScreen screen, EServerMode newServerMode, const std::vector<std::string> & playerNames)
{
	hostClientId = GameConnectionID::INVALID;
	setState(EClientState::NONE);
	serverMode = newServerMode;
	loadMode = ELoadMode::NONE;
	mapToStart = nullptr;
	hotseatMode = false;
	battleMode = false;
	th = std::make_unique<CStopWatch>();
	si = std::make_shared<StartInfo>();
	localPlayerNames.clear();
	si->difficulty = 1;
	si->mode = mode;
	screenType = screen;
	localPlayerNames.clear();
	if(!playerNames.empty()) //if have custom set of player names - use it
		localPlayerNames = playerNames;
	else
	{
		std::string playerName = settings["general"]["playerName"].String();
		if(playerName == "Player")
			playerName = LIBRARY->generaltexth->translate("core.genrltxt.434");
		localPlayerNames.push_back(playerName);
	}

	gameChat->resetMatchState();
	lobbyClient->resetMatchState();
}

GameChatHandler & CServerHandler::getGameChat()
{
	return *gameChat;
}

GlobalLobbyClient & CServerHandler::getGlobalLobby()
{
	return *lobbyClient;
}

INetworkHandler & CServerHandler::getNetworkHandler()
{
	return *networkHandler;
}

void CServerHandler::connectToServer(const std::string & addr, const ui16 port)
{
	connectToServerInternal(addr, port, true);
}

void CServerHandler::connectToServerInternal(const std::string & addr, const ui16 port, bool persistEndpoint)
{
	setState(EClientState::CONNECTING);
	serverHostname = addr;
	serverPort = port;

	if(addr.empty())
	{
		onConnectionFailed("No remote server has been configured");
		return;
	}

	const bool hasSessionEndpointOverride =
		(settings["session"]["serverhost"].isString() && !settings["session"]["serverhost"].String().empty())
		|| (settings["session"]["serverport"].isNumber() && settings["session"]["serverport"].Integer() > 0);
	if(persistEndpoint && !hasSessionEndpointOverride)
	{
		Settings remoteAddress = settings.write["server"]["remoteHostname"];
		remoteAddress->String() = addr;

		Settings remotePort = settings.write["server"]["remotePort"];
		remotePort->Integer() = port;
	}

	networkHandler->connectToRemote(*this, addr, port);
}

void CServerHandler::onConnectionFailed(const std::string & errorMessage)
{
	assert(getState() == EClientState::CONNECTING);
	std::scoped_lock interfaceLock(getInterfaceMutex());
	if(serverRedirectPending && serverRedirectAttempts < 40)
	{
		setState(EClientState::NONE);
		serverRedirectRetryScheduled = true;
		networkHandler->createTimer(redirectRetryTimer, std::chrono::milliseconds(250));
		return;
	}
	if(serverRedirectPending && serverRedirectPort != getRemotePort())
	{
		logNetwork->error("Arena match endpoint did not become ready; returning to public matchmaking server");
		serverRedirectPort = getRemotePort();
		serverRedirectMatchId = "matchmaking";
		serverRedirectAttempts = 0;
		setState(EClientState::NONE);
		serverRedirectRetryScheduled = true;
		networkHandler->createTimer(redirectRetryTimer, std::chrono::milliseconds(250));
		return;
	}
	serverRedirectPending = false;
	serverRedirectRetryScheduled = false;

	setState(EClientState::NONE);
	if(ENGINE)
	if(auto login = ENGINE->windows().topWindow<CLoginScreen>())
		login->onAuthenticationFailed("Could not connect to server.");
	else if(ENGINE)
		CInfoWindow::showInfoDialog(LIBRARY->generaltexth->translate("vcmi.mainMenu.serverConnectionFailed"), {});
	else
		GAME->onShutdownRequested(false);
}

void CServerHandler::onConnectionEstablished(const NetworkConnectionPtr & netConnection)
{
	assert(getState() == EClientState::CONNECTING);

	std::scoped_lock interfaceLock(getInterfaceMutex());

	networkConnection = netConnection;

	logNetwork->info("Connection established");

	if (serverMode == EServerMode::LOBBY_GUEST)
	{
		// say hello to lobby to switch connection to proxy mode
		getGlobalLobby().sendProxyConnectionLogin(netConnection);
	}

	logicConnection = std::make_shared<GameConnection>(netConnection);
	logicConnection->uuid = uuid;
	logicConnection->enterLobbyConnectionMode();
	setState(EClientState::AUTHENTICATING);
	if(serverRedirectPending)
	{
		sendAuthentication(authenticatedUsername, authenticatedPassword);
		return;
	}
	if(ENGINE)
	if(auto login = ENGINE->windows().topWindow<CLoginScreen>())
		login->onConnected();
}

void CServerHandler::applyPackOnLobbyScreen(CPackForLobby & pack)
{
	ApplyOnLobbyScreenNetPackVisitor visitor(*this, dynamic_cast<CLobbyScreen *>(SEL));
	pack.visit(visitor);
	if(ENGINE)
		ENGINE->windows().totalRedraw();
}

std::set<PlayerColor> CServerHandler::getHumanColors()
{
	return clientHumanColors(logicConnection->connectionID);
}

PlayerColor CServerHandler::myFirstColor() const
{
	return clientFirstColor(logicConnection->connectionID);
}

bool CServerHandler::isMyColor(PlayerColor color) const
{
	return isClientColor(logicConnection->connectionID, color);
}

PlayerConnectionID CServerHandler::myFirstId() const
{
	return clientFirstId(logicConnection->connectionID);
}

EClientState CServerHandler::getState() const
{
	return state;
}

void CServerHandler::setState(EClientState newState)
{
	state = newState;
}

bool CServerHandler::isHost() const
{
	return logicConnection && hostClientId == logicConnection->connectionID;
}

bool CServerHandler::isGuest() const
{
	return !logicConnection || hostClientId != logicConnection->connectionID;
}

bool CServerHandler::hasRemoteClientInLobby() const
{
	for(const auto & playerEntry : playerNames)
	{
		const auto connectionId = playerEntry.second.connection;
		if(connectionId != GameConnectionID::INVALID && connectionId != hostClientId)
			return true;
	}

	return false;
}

const std::string & CServerHandler::getRemoteHostname() const
{
	if(settings["session"]["serverhost"].isString() && !settings["session"]["serverhost"].String().empty())
		return settings["session"]["serverhost"].String();
	return settings["server"]["remoteHostname"].String();
}

ui16 CServerHandler::getRemotePort() const
{
	if(settings["session"]["serverport"].isNumber() && settings["session"]["serverport"].Integer() > 0)
		return static_cast<ui16>(settings["session"]["serverport"].Integer());
	return settings["server"]["remotePort"].Integer();
}

const std::string & CServerHandler::getCurrentHostname() const
{
	return serverHostname;
}

ui16 CServerHandler::getCurrentPort() const
{
	return serverPort;
}

void CServerHandler::sendClientConnecting() const
{
	LobbyClientConnected lcc;
	lcc.uuid = uuid;
	lcc.names = localPlayerNames;
	lcc.mode = si->mode;
	lcc.arenaSinglePlayer = loadMode == ELoadMode::SINGLE;
	sendLobbyPack(lcc);
}

EAccountRole CServerHandler::getAccountRole() const
{
	return accountRole;
}

void CServerHandler::sendAuthentication(const std::string & username, const std::string & password)
{
	if(getState() != EClientState::AUTHENTICATING)
		return;

	LobbyAuthRequest request;
	request.username = username;
	request.password = password;
	if(serverRedirectPending && serverRedirectMatchId != "matchmaking")
	{
		request.relayMatchId = serverRedirectMatchId;
		request.relayToken = serverRedirectToken;
	}
	authenticatedUsername = username;
	authenticatedPassword = password;
	sendLobbyPack(request);
	scheduleAuthenticationTimeout();
}

void CServerHandler::handleAuthenticationResult(const LobbyAuthResult & result)
{
	if(getState() != EClientState::AUTHENTICATING)
		return;

	if(result.result == EAuthResult::ACCEPTED)
	{
		accountRole = result.role;
		setState(EClientState::NONE);
		if(serverRedirectPending)
		{
			logNetwork->info("Authenticated redirected arena endpoint for match '%s'", serverRedirectMatchId);
			serverRedirectPending = false;
			serverRedirectRetryScheduled = false;
			serverRedirectAttempts = 0;
			serverRedirectMatchId.clear();
			serverRedirectToken.clear();
			sendClientConnecting();
			return;
		}
		if(ENGINE)
		if(auto login = ENGINE->windows().topWindow<CLoginScreen>())
			login->onAuthenticationAccepted();
		return;
	}
	if(result.result == EAuthResult::SERVER_BUSY && serverRedirectPending && serverRedirectMatchId != "matchmaking")
	{
		retryPublicMatchmakingAfterRejectedRedirect();
		return;
	}

	setState(EClientState::AUTH_REJECTED);
	std::string reason;
	switch(result.result)
	{
	case EAuthResult::BAD_CREDENTIALS: reason = "Incorrect username or password."; break;
	case EAuthResult::SERVER_BUSY: reason = "Server is busy."; break;
	case EAuthResult::ACCOUNT_IN_USE: reason = LIBRARY->generaltexth->translate("vcmi.arena.login.accountInUse"); break;
	default: reason = "Server authentication error."; break;
	}
	if(ENGINE)
	if(auto login = ENGINE->windows().topWindow<CLoginScreen>())
		login->onAuthenticationFailed(reason);
	if(ENGINE && result.result == EAuthResult::ACCOUNT_IN_USE)
	{
		logNetwork->info("Authentication rejected: account is already in use");
		CInfoWindow::showInfoDialog(reason, {});
	}
}

void CServerHandler::handleServerRedirect(const LobbyServerRedirect & redirect)
{
	if(redirect.port == 0 || authenticatedUsername.empty() || authenticatedPassword.empty())
	{
		logNetwork->error("Rejected invalid arena server redirect");
		return;
	}

	serverRedirectPending = true;
	serverRedirectRetryScheduled = false;
	serverRedirectPort = redirect.port;
	serverRedirectAttempts = 0;
	serverRedirectMatchId = redirect.matchId;
	serverRedirectToken = redirect.relayToken;
	setState(EClientState::CONNECTION_CANCELLED);
	logNetwork->info("Arena matchmaking redirect: match='%s' port=%d", redirect.matchId, redirect.port);
	if(networkConnection)
		networkConnection->close();
}

void CServerHandler::connectToRedirectServer()
{
	serverRedirectRetryScheduled = false;
	serverRedirectAttempts++;
	logicConnection.reset();
	networkConnection.reset();
	connectToServerInternal(getRemoteHostname(), serverRedirectPort, false);
}

void CServerHandler::retryPublicMatchmakingAfterRejectedRedirect()
{
	logNetwork->warn("Arena match '%s' is no longer available; returning to public matchmaking", serverRedirectMatchId);
	serverRedirectPort = getRemotePort();
	serverRedirectMatchId = "matchmaking";
	serverRedirectToken.clear();
	serverRedirectAttempts = 0;
	setState(EClientState::CONNECTION_CANCELLED);
	if(networkConnection)
	{
		networkConnection->close();
		return;
	}

	setState(EClientState::NONE);
	serverRedirectRetryScheduled = true;
	networkHandler->createTimer(redirectRetryTimer, std::chrono::milliseconds(1));
}

void CServerHandler::onRedirectRetryTimer()
{
	std::scoped_lock interfaceLock(getInterfaceMutex());
	if(serverRedirectRetryScheduled && serverRedirectPending)
		connectToRedirectServer();
}

void CServerHandler::scheduleAuthenticationTimeout()
{
	authenticationTimeoutDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	if(authenticationTimeoutScheduled)
		return;

	authenticationTimeoutScheduled = true;
	networkHandler->createTimer(authenticationTimeoutTimer, std::chrono::seconds(10));
}

void CServerHandler::onAuthenticationTimeoutTimer()
{
	std::scoped_lock interfaceLock(getInterfaceMutex());
	if(!authenticationTimeoutScheduled)
		return;

	const auto now = std::chrono::steady_clock::now();
	if(now < authenticationTimeoutDeadline)
	{
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(authenticationTimeoutDeadline - now) + std::chrono::milliseconds(1);
		networkHandler->createTimer(authenticationTimeoutTimer, remaining);
		return;
	}

	authenticationTimeoutScheduled = false;
	if(getState() != EClientState::AUTHENTICATING)
		return;
	setState(EClientState::AUTH_REJECTED);
	if(ENGINE)
	if(auto login = ENGINE->windows().topWindow<CLoginScreen>())
		login->onAuthenticationFailed("Authentication timed out. Please try again.");
	if(networkConnection)
		networkConnection->close();
}

void CServerHandler::sendClientDisconnecting()
{
	if(!logicConnection)
	{
		return;
	}
	setState(EClientState::NONE);
	mapToStart = nullptr;
	LobbyClientDisconnected lcd;
	lcd.clientId = logicConnection->connectionID;
	logNetwork->info("Sent leaving-lobby signal to the server.");
	sendLobbyPack(lcd);
}

void CServerHandler::requestGameAbandon()
{
	if(getState() != EClientState::GAMEPLAY)
	{
		logNetwork->info("Ignoring game-abandon request outside gameplay.");
		return;
	}

	setState(EClientState::ABANDONING_GAME);
	LobbyClientAbandonGame pack;
	logNetwork->info("Sent game-abandon request to the server.");
	sendLobbyPack(pack);
}

void CServerHandler::setCampaignState(std::shared_ptr<CampaignState> newCampaign)
{
	setState(EClientState::LOBBY_CAMPAIGN);
	LobbySetCampaign lsc;
	lsc.ourCampaign = newCampaign;
	sendLobbyPack(lsc);
}

void CServerHandler::setCampaignMap(CampaignScenarioID mapId) const
{
	if(getState() == EClientState::GAMEPLAY) // FIXME: UI shouldn't sent commands in first place
		return;

	LobbySetCampaignMap lscm;
	lscm.mapId = mapId;
	sendLobbyPack(lscm);
}

void CServerHandler::setCampaignBonus(int bonusId) const
{
	if(getState() == EClientState::GAMEPLAY) // FIXME: UI shouldn't sent commands in first place
		return;

	LobbySetCampaignBonus lscb;
	lscb.bonusId = bonusId;
	sendLobbyPack(lscb);
}

void CServerHandler::setBattleOnlyModeStartInfo(std::shared_ptr<BattleOnlyModeStartInfo> startInfo) const
{
	LobbySetBattleOnlyModeStartInfo lsbomsui;
	lsbomsui.startInfo = startInfo;
	sendLobbyPack(lsbomsui);
}

void CServerHandler::setMapInfo(std::shared_ptr<CMapInfo> to, std::shared_ptr<CMapGenOptions> mapGenOpts) const
{
	LobbySetMap lsm;
	lsm.mapInfo = to;
	lsm.mapGenOpts = mapGenOpts;
	sendLobbyPack(lsm);
}

void CServerHandler::setPlayer(PlayerColor color) const
{
	LobbySetPlayer lsp;
	lsp.clickedColor = color;
	sendLobbyPack(lsp);
}

void CServerHandler::setPlayerReady(bool ready) const
{
	LobbySetPlayerReady lsp;
	lsp.ready = ready;
	sendLobbyPack(lsp);
}

void CServerHandler::setPlayerName(PlayerColor color, const std::string & name) const
{
	LobbySetPlayerName lspn;
	lspn.color = color;
	lspn.name = name;
	sendLobbyPack(lspn);
}

void CServerHandler::setPlayerHandicap(PlayerColor color, Handicap handicap) const
{
	LobbySetPlayerHandicap lsph;
	lsph.color = color;
	lsph.handicap = handicap;
	sendLobbyPack(lsph);
}

void CServerHandler::setPlayerOption(ui8 what, int32_t value, PlayerColor player) const
{
	LobbyChangePlayerOption lcpo;
	lcpo.what = what;
	lcpo.value = value;
	lcpo.color = player;
	sendLobbyPack(lcpo);
}

void CServerHandler::setDifficulty(int to) const
{
	LobbySetDifficulty lsd;
	lsd.difficulty = to;
	sendLobbyPack(lsd);
}

void CServerHandler::setSimturnsInfo(const SimturnsInfo & info) const
{
	LobbySetSimturns pack;
	pack.simturnsInfo = info;
	sendLobbyPack(pack);
}

void CServerHandler::setTurnTimerInfo(const TurnTimerInfo & info) const
{
	LobbySetTurnTime lstt;
	lstt.turnTimerInfo = info;
	sendLobbyPack(lstt);
}

void CServerHandler::setExtraOptionsInfo(const ExtraOptionsInfo & info) const
{
	LobbySetExtraOptions lseo;
	lseo.extraOptionsInfo = info;
	sendLobbyPack(lseo);
}

void CServerHandler::sendMessage(const std::string & txt) const
{
	std::istringstream readed;
	readed.str(txt);
	std::string command;
	readed >> command;
	if(command == "!passhost")
	{
		std::string id;
		readed >> id;
		if(id.length())
		{
			LobbyChangeHost lch;
			lch.newHostConnectionId = static_cast<GameConnectionID>(boost::lexical_cast<int>(id));
			sendLobbyPack(lch);
		}
	}
	else if(command == "!forcep")
	{
		std::string connectedId;
		std::string playerColorId;
		readed >> connectedId;
		readed >> playerColorId;
		if(connectedId.length() && playerColorId.length())
		{
			auto connected = static_cast<PlayerConnectionID>(boost::lexical_cast<int>(connectedId));
			auto color = PlayerColor(boost::lexical_cast<int>(playerColorId));
			if(color.isValidPlayer() && playerNames.find(connected) != playerNames.end())
			{
				LobbyForceSetPlayer lfsp;
				lfsp.targetConnectedPlayer = connected;
				lfsp.targetPlayerColor = color;
				sendLobbyPack(lfsp);
			}
		}
	}
	else
	{
		gameChat->sendMessageLobby(playerNames.find(myFirstId())->second.name, txt);
	}
}

void CServerHandler::sendGuiAction(ui8 action) const
{
	LobbyGuiAction lga;
	lga.action = static_cast<LobbyGuiAction::EAction>(action);
	sendLobbyPack(lga);
}

void CServerHandler::sendRestartGame() const
{
	if(si->campState && !si->campState->getLoadingBackground().empty())
		ENGINE->windows().createAndPushWindow<CLoadingScreen>(si->campState->getLoadingBackground());
	else
		ENGINE->windows().createAndPushWindow<CLoadingScreen>();
	
	LobbyRestartGame endGame;
	sendLobbyPack(endGame);
}

bool CServerHandler::validateGameStart(bool allowOnlyAI) const
{
	try
	{
		verifyStateBeforeStart(allowOnlyAI ? true : settings["session"]["onlyai"].Bool());
	}
	catch(ModIncompatibility & e)
	{
		logGlobal->warn("Incompatibility exception during start scenario: %s", e.what());

		showServerError(e.getFullErrorMsg());
		return false;
	}
	catch(std::exception & e)
	{
		logGlobal->error("Exception during startScenario: %s", e.what());
		MetaString message;
		message.appendTextID("vcmi.lobby.system.unableStartMap");
		message.appendRawString("\n");
		message.appendTextID("vcmi.lobby.system.reason");
		message.replaceRawString(e.what());
		showServerError(message.toString());
		return false;
	}

	return true;
}

void CServerHandler::sendStartGame(bool allowOnlyAI, bool verify) const
{
	if(verify)
		verifyStateBeforeStart(allowOnlyAI ? true : settings["session"]["onlyai"].Bool());

	if(!settings["session"]["headless"].Bool())
	{
		if(si->campState && !si->campState->getLoadingBackground().empty())
			ENGINE->windows().createAndPushWindow<CLoadingScreen>(si->campState->getLoadingBackground());
		else
			ENGINE->windows().createAndPushWindow<CLoadingScreen>();
	}
	
	LobbyPrepareStartGame lpsg;
	sendLobbyPack(lpsg);

	LobbyStartGame lsg;
	sendLobbyPack(lsg);
}

void CServerHandler::startMapAfterConnection(std::shared_ptr<CMapInfo> to)
{
	mapToStart = to;
}

void CServerHandler::startGameplay(std::shared_ptr<CGameState> gameState)
{
	if(GAME->mainmenu())
		GAME->mainmenu()->disable();

	switch(si->mode)
	{
	case EStartMode::NEW_GAME:
		client->newGame(gameState);
		break;
	case EStartMode::CAMPAIGN:
		if(si->campState->conqueredScenarios().empty())
			si->campState->highscoreParameters.clear();
		client->newGame(gameState);
		break;
	case EStartMode::LOAD_GAME:
		client->loadGame(gameState);
		break;
	default:
		throw std::runtime_error("Invalid mode");
	}

	if(ENGINE)
		ENGINE->discord().setPlayingStatus(si, &gameState->getMap(), howManyPlayerInterfaces());

	// After everything initialized we can accept CPackToClient netpacks
	setState(EClientState::GAMEPLAY);
}

void CServerHandler::showHighScoresAndEndGameplay(PlayerColor player, bool victory, const StatisticDataSet & statistic)
{
	HighScoreParameter param = HighScore::prepareHighScores(&client->gameState(), player, victory);

	if(victory && client->gameState().getStartInfo()->campState)
	{
		startCampaignScenario(param, client->gameState().getStartInfo()->campState, statistic);
	}
	else
	{
		HighScoreCalculation scenarioHighScores;
		scenarioHighScores.parameters.push_back(param);
		scenarioHighScores.isCampaign = false;

		endGameplay();
		GAME->mainmenu()->menu->switchToTab("main");
		ENGINE->windows().createAndPushWindow<CHighScoreInputScreen>(victory, scenarioHighScores, statistic);
	}
}

void CServerHandler::endGameplay()
{
	clearArenaDisconnectWaitWindow();
	setState(EClientState::NONE);
	if(logicConnection)
		logicConnection->enterLobbyConnectionMode();

	client->finishGameplay();

	client->endGame();
	client.reset();

	if (GAME->mainmenu())
	{
		GAME->mainmenu()->enable();
		GAME->mainmenu()->playMusic();
		GAME->mainmenu()->makeActiveInterface();
	}

	ENGINE->discord().setStatus("", "", {0, 0});
}

std::optional<std::string> CServerHandler::canQuickLoadGame(const std::string & path) const
{
	auto mapInfo = std::make_shared<CMapInfo>();
	try
	{
		mapInfo->saveInit(ResourcePath(path, EResType::SAVEGAME));
	}
	catch(const IdentifierResolutionException & e)
	{
		return "Identifier not found.";
	}
	catch(const std::exception & e)
	{
		return "Generic error loading save.";
	}

	// initial start info from quick load slot
	const auto * startInfo1 = mapInfo->scenarioOptionsOfSave.get();
	// initial start info from game state (not current start info)
	const auto * startInfo2 = client->gameState().getInitialStartInfo();

	if (!startInfo1)
		return "Missing quick load start info.";
	if (!startInfo2)
		return "Missing server start info.";
	if (startInfo1->startTime != startInfo2->startTime)
		return "Different initial start time.";
	if (startInfo1->mapname != startInfo2->mapname)
		return "Different map name.";
	if (startInfo1->difficulty != startInfo2->difficulty)
		return "Different difficulty.";
	const auto & playerInfos1 = startInfo1->playerInfos;
	const auto & playerInfos2 = startInfo2->playerInfos;
	if (playerInfos1.size() != playerInfos2.size())
		return "Different number of players.";
	if (!std::equal(playerInfos1.begin(), playerInfos1.end(), playerInfos2.begin(), playerInfos2.end(),
		[](const auto& p1, const auto& p2) { return p1.first == p2.first && p1.second.connectedPlayerIDs == p2.second.connectedPlayerIDs; }))
		return "Different players.";
	return std::nullopt;
}

void CServerHandler::quickLoadGame(const std::string & path)
{
	LobbyQuickLoadGame pack;
	pack.saveFilePath = path;
	sendLobbyPack(pack);
}

void CServerHandler::restartGameplay()
{
	clearArenaDisconnectWaitWindow();
	if(client)
	{
		client->finishGameplay();
		client->endGame();
		client.reset();
	}

	if(logicConnection)
		logicConnection->enterLobbyConnectionMode();
}

void CServerHandler::startCampaignScenario(HighScoreParameter param, std::shared_ptr<CampaignState> cs, const StatisticDataSet & statistic)
{
	std::shared_ptr<CampaignState> ourCampaign = cs;

	if (!cs)
		ourCampaign = si->campState;

	param.campaignName = cs->getNameTranslated();
	cs->highscoreParameters.push_back(param);
	auto campaignScoreCalculator = std::make_shared<HighScoreCalculation>();
	campaignScoreCalculator->isCampaign = true;
	campaignScoreCalculator->parameters = cs->highscoreParameters;

	endGameplay();

	auto & epilogue = ourCampaign->scenario(*ourCampaign->lastScenario()).epilog;
	auto finisher = [ourCampaign, campaignScoreCalculator, statistic]()
	{
		if(ourCampaign->campaignSet != "" && ourCampaign->isCampaignFinished())
		{
			Settings entry = persistentStorage.write["completedCampaigns"][ourCampaign->getFilename()];
			entry->Bool() = true;
		}

		if(!ourCampaign->isCampaignFinished())
			GAME->mainmenu()->openCampaignLobby(ourCampaign);
		else
		{
			GAME->mainmenu()->openCampaignScreen(ourCampaign->campaignSet);
			if(!ourCampaign->getOutroVideo().empty() && ENGINE->video().open(ourCampaign->getOutroVideo(), 1))
			{
				ENGINE->music().stopMusic();
				auto rim = ourCampaign->getVideoRim().empty() ? ImagePath::builtin("INTRORIM") : ourCampaign->getVideoRim();
				if(ourCampaign->getVideoRim() == ImagePath::builtin("NONE"))
					rim = ImagePath();
				ENGINE->windows().createAndPushWindow<VideoWindow>(ourCampaign->getOutroVideo(), rim, false, 1, [campaignScoreCalculator, statistic](bool skipped){
					ENGINE->windows().createAndPushWindow<CHighScoreInputScreen>(true, *campaignScoreCalculator, statistic);
				});
			}
			else
				ENGINE->windows().createAndPushWindow<CHighScoreInputScreen>(true, *campaignScoreCalculator, statistic);
		}
	};

	if(epilogue.hasPrologEpilog)
	{
		ENGINE->windows().createAndPushWindow<CPrologEpilogVideo>(epilogue, finisher);
	}
	else
	{
		finisher();
	}
}

void CServerHandler::showServerError(const std::string & txt) const
{
	if(auto w = ENGINE->windows().topWindow<CLoadingScreen>())
		ENGINE->windows().popWindow(w);
	
	CInfoWindow::showInfoDialog(txt, {});
}

int CServerHandler::howManyPlayerInterfaces()
{
	int playerInts = 0;
	for(auto pint : client->playerint)
	{
		if(dynamic_cast<CPlayerInterface *>(pint.second.get()))
			playerInts++;
	}

	return playerInts;
}

ELoadMode CServerHandler::getLoadMode()
{
	if(loadMode != ELoadMode::TUTORIAL && getState() == EClientState::GAMEPLAY)
	{
		if(si->campState)
			return ELoadMode::CAMPAIGN;
		for(auto pn : playerNames)
		{
			if(pn.second.connection != logicConnection->connectionID)
				return ELoadMode::MULTI;
		}
		if(howManyPlayerInterfaces() > 1)  //this condition will work for hotseat mode OR multiplayer with allowed more than 1 color per player to control
			return ELoadMode::MULTI;

		return ELoadMode::SINGLE;
	}
	return loadMode;
}

void CServerHandler::debugStartTest(std::string filename, bool save, int32_t difficultyIndex)
{
	logGlobal->info("Starting debug test with file: %s", filename);
	auto mapInfo = std::make_shared<CMapInfo>();
	if(save)
	{
		resetStateForLobby(EStartMode::LOAD_GAME, ESelectionScreen::loadGame, EServerMode::LOCAL, {});
		mapInfo->saveInit(ResourcePath(filename, EResType::SAVEGAME));
	}
	else
	{
		resetStateForLobby(EStartMode::NEW_GAME, ESelectionScreen::newGame, EServerMode::LOCAL, {});
		if(difficultyIndex >= 0)
		{
			if(difficultyIndex > 4)
				throw std::runtime_error("test difficulty index must be in range 0..4");
			si->difficulty = static_cast<ui8>(difficultyIndex);
		}
		try
		{
			mapInfo->mapInit(filename);
		}
		catch(const std::exception & e)
		{
			// A test map must never fall back to the lobby's last selected map.
			// Make an invalid --testmap argument visible in the log and terminate startup.
			logGlobal->error("Unable to start test map '%s': %s", filename, e.what());
			GAME->onShutdownRequested(false);
			return;
		}
	}
	connectToServer(getRemoteHostname(), getRemotePort());

	const auto testMatchmaking = settings["session"]["testArenaMatchmaking"].String();
	if(!testMatchmaking.empty())
		loadMode = testMatchmaking == "single" ? ELoadMode::SINGLE : ELoadMode::MULTI;
	const auto connectionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while(getState() == EClientState::CONNECTING && std::chrono::steady_clock::now() < connectionDeadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	if(getState() != EClientState::AUTHENTICATING)
	{
		logGlobal->error("Unable to authenticate debug test: server connection was not established.");
		GAME->onShutdownRequested(false);
		return;
	}

	std::string profileName = settings["session"]["loginProfile"].String();
	profileName.erase(std::remove_if(profileName.begin(), profileName.end(), [](unsigned char value)
	{
		return !std::isalnum(value) && value != '-' && value != '_';
	}), profileName.end());
	const auto profilePath = VCMIDirs::get().userConfigPath() / "loginProfiles" / (profileName + ".json");
	try
	{
		const char * simulationToken=std::getenv("VCMI_NATIVE_SIM_TOKEN");
		if(simulationToken && std::getenv("VCMI_NATIVE_SIM_ROOT"))
		{
			const std::string secret(simulationToken);
			if(getRemoteHostname()!="127.0.0.1"||secret.size()!=64||secret.find_first_not_of("0123456789ABCDEFabcdef")!=std::string::npos)
				throw std::runtime_error("invalid private simulation authentication context");
			sendAuthentication("NativeSimulation",secret);
		}
		else
		{
		if(profileName.empty() || !boost::filesystem::is_regular_file(profilePath))
			throw std::runtime_error("a saved --loginProfile is required");
		std::ifstream input(profilePath.string(), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		const JsonNode profile(contents.data(), contents.size(), profilePath.string());
		const std::string username = profile["username"].String();
		const std::string password = profile["password"].String();
		if(username.empty() || password.empty())
			throw std::runtime_error("the selected login profile has no saved credentials");
		sendAuthentication(username, password);
		}
	}
	catch(const std::exception & error)
	{
		logGlobal->error("Unable to authenticate debug test with profile '%s': %s", profileName, error.what());
		GAME->onShutdownRequested(false);
		return;
	}

	const auto authenticationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while(getState() == EClientState::AUTHENTICATING && std::chrono::steady_clock::now() < authenticationDeadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	if(getState() != EClientState::NONE)
	{
		logGlobal->error("Unable to authenticate debug test with profile '%s'.", profileName);
		GAME->onShutdownRequested(false);
		return;
	}
	sendClientConnecting();

	if(!testMatchmaking.empty())
	{
		// Exercise the real ready protocol; do not bypass server permissions.
		// The dedicated server owns map selection. Only the host changes options.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
		const size_t participants = testMatchmaking == "single" ? 1 : 2;
		while(getState() != EClientState::LOBBY || playerNames.size() < participants)
		{
			if(std::chrono::steady_clock::now() >= deadline)
			{
				logGlobal->error("Local matchmaking test timed out waiting for participants");
				GAME->onShutdownRequested(false);
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if(isHost())
			setMapInfo(mapInfo);
		while(!mi)
		{
			if(std::chrono::steady_clock::now() >= deadline)
			{
				logGlobal->error("Local matchmaking test timed out waiting for the server map");
				GAME->onShutdownRequested(false);
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if(difficultyIndex >= 0)
		{
			if(isHost())
				setDifficulty(difficultyIndex);
			while(si->difficulty != difficultyIndex)
			{
				if(std::chrono::steady_clock::now() >= deadline)
				{
					logGlobal->error("Local matchmaking test timed out waiting for difficulty");
					GAME->onShutdownRequested(false);
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}
		setPlayerReady(true);
		return;
	}

	while(!settings["session"]["headless"].Bool() && !ENGINE->windows().topWindow<CLobbyScreen>())
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// The lobby initializes its default tabs asynchronously and may temporarily
	// select general.lastMap or BattleOnlyMode. Wait for that initialization to
	// settle before overriding it with the requested test fixture.
	std::this_thread::sleep_for(std::chrono::seconds(2));
	// A dedicated arena server is authoritative over the map and replaces the
	// requested fixture with Maps/BattleOnlyMode. Send the request once, then
	// wait for the server-confirmed map instead of comparing it to the local URI.
	setMapInfo(mapInfo);
	const auto mapSelectionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while(!mi)
	{
		if(std::chrono::steady_clock::now() >= mapSelectionDeadline)
		{
			logGlobal->error("Unable to select test map '%s' in the local lobby.", mapInfo->fileURI);
			GAME->onShutdownRequested(false);
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if(!save && difficultyIndex >= 0)
	{
		setDifficulty(difficultyIndex);
		const auto difficultyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while(si->difficulty != difficultyIndex && std::chrono::steady_clock::now() < difficultyDeadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if(si->difficulty != difficultyIndex)
		{
			logGlobal->error("Unable to set test difficulty to index %d.", difficultyIndex);
			GAME->onShutdownRequested(false);
			return;
		}
	}
	// Keep the authenticated client assigned to its human lobby slot. The
	// session/onlyai flag installs an AI interface for that color after start;
	// relinquishing the slot here would leave no authorized human participant
	// and the dedicated arena server would correctly reject LobbyStartGame.

	while(true)
	{
		try
		{
			sendStartGame();
			break;
		}
		catch(...)
		{

		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

class ServerHandlerCPackVisitor : public VCMI_LIB_WRAP_NAMESPACE(ICPackVisitor)
{
private:
	CServerHandler & handler;

public:
	ServerHandlerCPackVisitor(CServerHandler & handler)
			:handler(handler)
	{
	}

	bool callTyped() override { return false; }

	void visitForLobby(CPackForLobby & lobbyPack) override
	{
		handler.visitForLobby(lobbyPack);
	}

	void visitForClient(CPackForClient & clientPack) override
	{
		handler.visitForClient(clientPack);
	}
};

void CServerHandler::onPacketReceived(const std::shared_ptr<INetworkConnection> &, const std::vector<std::byte> & message)
{
	std::scoped_lock interfaceLock(getInterfaceMutex());

	if(getState() == EClientState::DISCONNECTING)
		return;

	auto pack = logicConnection->retrievePack(message);
	ServerHandlerCPackVisitor visitor(*this);
	pack->visit(visitor);
}

void CServerHandler::onDisconnected(const std::shared_ptr<INetworkConnection> & connection, const std::string & errorMessage)
{
	std::scoped_lock interfaceLock(getInterfaceMutex());

	if (connection != networkConnection)
	{
		// ServerHandler already closed this connection on its own
		// This is the final call from network thread that informs serverHandler that connection has died
		// ignore it since serverHandler have already shut down this connection (and possibly started a new one)
		return;
	}

	if(getState() == EClientState::DISCONNECTING)
	{
		// Note: this branch can be reached on app shutdown, when main thread holds mutex till destruction
		logNetwork->info("Successfully closed connection to server!");
		return;
	}

	if(getState() == EClientState::AUTH_REJECTED)
	{
		networkConnection.reset();
		logicConnection.reset();
		setState(EClientState::NONE);
		return;
	}

	if(getState() == EClientState::AUTHENTICATING)
	{
		setState(EClientState::NONE);
		if(ENGINE)
		if(auto login = ENGINE->windows().topWindow<CLoginScreen>())
			login->onAuthenticationFailed("Connection closed while authenticating.");
		networkConnection.reset();
		logicConnection.reset();
		return;
	}

	if(serverRedirectPending)
	{
		// Matchmaking intentionally closes the coordinator connection before
		// opening the assigned worker connection. Defer the new connection to
		// the network timer so this disconnect callback can finish cleanly.
		networkConnection.reset();
		logicConnection.reset();
		setState(EClientState::NONE);
		serverRedirectRetryScheduled = true;
		networkHandler->createTimer(redirectRetryTimer, std::chrono::milliseconds(1));
		return;
	}

	logNetwork->error("Lost connection to server! Connection has been closed");

	if(client)
	{
		endGameplay();
		GAME->mainmenu()->menu->switchToTab("main");
		showServerError(LIBRARY->generaltexth->translate("vcmi.server.errors.disconnected"));
	}
	else
	{
		LobbyClientDisconnected lcd;
		lcd.clientId = logicConnection->connectionID;
		applyPackOnLobbyScreen(lcd);
	}

	networkConnection.reset();
}


void CServerHandler::visitForLobby(CPackForLobby & lobbyPack)
{
	ApplyOnLobbyHandlerNetPackVisitor visitor(*this);
	lobbyPack.visit(visitor);

	if(visitor.getResult())
	{
		if(!settings["session"]["headless"].Bool())
			applyPackOnLobbyScreen(lobbyPack);
	}
}

void CServerHandler::visitForClient(CPackForClient & clientPack)
{
	if(getState() != EClientState::STARTING && getState() != EClientState::GAMEPLAY && getState() != EClientState::ABANDONING_GAME)
	{
		logNetwork->debug("Dropping delayed gameplay packet in client state %d", static_cast<int>(getState()));
		return;
	}

	if(serverRedirectPending && getState() == EClientState::CONNECTION_CANCELLED)
	{
		connectToRedirectServer();
		return;
	}

	client->handlePack(clientPack);
}

void CServerHandler::sendLobbyPack(const CPackForLobby & pack) const
{
	if(getState() != EClientState::STARTING)
		logicConnection->sendPack(pack);
}

bool CServerHandler::inLobbyRoom() const
{
	return serverMode == EServerMode::LOBBY_HOST || serverMode == EServerMode::LOBBY_GUEST;
}

bool CServerHandler::inGame() const
{
	return logicConnection != nullptr;
}

void CServerHandler::sendGamePack(const CPackForServer & pack) const
{
	logicConnection->sendPack(pack);
}
