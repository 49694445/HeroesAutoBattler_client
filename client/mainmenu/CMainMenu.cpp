/*
 * CMainMenu.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CMainMenu.h"

#include "../../lib/network/NetworkDiscovery.h"
#include "CCampaignScreen.h"
#include "CHighScoreScreen.h"
#include "CreditsScreen.h"

#include "../lobby/CBonusSelection.h"
#include "../lobby/CSelectionBase.h"
#include "../lobby/CLobbyScreen.h"
#include "../media/IMusicPlayer.h"
#include "../media/IVideoPlayer.h"
#include "../gui/CursorHandler.h"
#include "../windows/GUIClasses.h"
#include "../windows/CMessage.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../eventsSDL/InputHandler.h"
#include "../gui/ShortcutHandler.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../render/Canvas.h"
#include "../globalLobby/GlobalLobbyLoginWindow.h"
#include "../globalLobby/GlobalLobbyClient.h"
#include "../globalLobby/GlobalLobbyWindow.h"
#include "../widgets/CComponent.h"
#include "../widgets/Buttons.h"
#include "../widgets/ComboBox.h"
#include "../widgets/CTextInput.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../widgets/VideoWidget.h"
#include "../windows/InfoWindows.h"
#include "../windows/wiki/WikiWindow.h"
#include "../CServerHandler.h"

#include "../CPlayerInterface.h"
#include "../Client.h"
#include "../CMT.h"

#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/campaign/CampaignHandler.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/CCompressedStream.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/json/JsonUtils.h"

#include <boost/lexical_cast.hpp>

namespace
{
boost::filesystem::path savedLoginProfilePath()
{
	std::string profile = settings["session"]["loginProfile"].String();
	profile.erase(std::remove_if(profile.begin(), profile.end(), [](unsigned char value)
	{
		return !std::isalnum(value) && value != '-' && value != '_';
	}), profile.end());
	if(profile.empty())
		return {};
	return VCMIDirs::get().userConfigPath() / "loginProfiles" / (profile + ".json");
}

JsonNode loadSavedLoginProfile()
{
	const auto path = savedLoginProfilePath();
	if(path.empty() || !boost::filesystem::is_regular_file(path))
		return JsonNode(JsonMap{});

	try
	{
		std::ifstream input(path.string(), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		return JsonNode(contents.data(), contents.size(), path.string());
	}
	catch(const std::exception & error)
	{
		logGlobal->warn("Unable to read saved login profile '%s': %s", path, error.what());
		return JsonNode(JsonMap{});
	}
}

void saveLoginProfile(const std::string & username, const std::string & password, bool remember)
{
	const auto path = savedLoginProfilePath();
	if(path.empty())
		return;

	try
	{
		boost::filesystem::create_directories(path.parent_path());
		JsonNode profile(JsonMap{});
		profile["username"].String() = username;
		profile["password"].String() = remember ? password : "";
		profile["remember"].Bool() = remember;
		std::ofstream output(path.string(), std::ios::binary | std::ios::trunc);
		output << profile.toString();
	}
	catch(const std::exception & error)
	{
		logGlobal->warn("Unable to save login profile '%s': %s", path, error.what());
	}
}
}

ISelectionScreenInfo * SEL = nullptr;

CMenuScreen::CMenuScreen(const JsonNode & configNode)
	: CWindowObject(BORDERED), config(configNode)
{
	OBJECT_CONSTRUCTION;
	addUsedEvents(KEYBOARD);

	const auto& bgConfig = config["background"];
	if (bgConfig.isVector())
		background = std::make_shared<CPicture>(ImagePath::fromJson(*RandomGeneratorUtil::nextItem(bgConfig.Vector(), CRandomGenerator::getDefault())));

	if (bgConfig.isString())
		background = std::make_shared<CPicture>(ImagePath::fromJson(bgConfig));

	if(config["scalable"].Bool())
		background->scaleTo(ENGINE->screenDimensions());

	pos = background->center();

	for (const JsonNode& node : config["images"].Vector())
	{
		auto image = std::make_shared<CPicture>(ImagePath::fromJson(*RandomGeneratorUtil::nextItem(node["name"].Vector(), CRandomGenerator::getDefault())), adjustNegativeCoordinate(node["x"].Integer(), node["y"].Integer()));
		images.push_back(image);
	}

	if(!config["video"].isNull())
	{
		Point videoPosition = adjustNegativeCoordinate(config["video"]["x"].Integer(), config["video"]["y"].Integer());
		videoPlayer = std::make_shared<VideoWidget>(videoPosition, VideoPath::fromJson(config["video"]["name"]), false);
	}

	for(const JsonNode & node : config["items"].Vector())
		menuNameToEntry.push_back(node["name"].String());

	//Hardcoded entry
	menuNameToEntry.push_back("credits");

	tabs = std::make_shared<CTabbedInt>(std::bind(&CMenuScreen::createTab, this, _1));
	if(config["video"].isNull())
		tabs->setRedrawParent(true);

}

std::shared_ptr<CIntObject> CMenuScreen::createTab(size_t index)
{
	if(config["items"].Vector().size() == index)
		return std::make_shared<CreditsScreen>(this->pos);
	else
		return std::make_shared<CMenuEntry>(this, config["items"].Vector()[index]);
}

void CMenuScreen::show(Canvas & to)
{
	// TODO: avoid excessive redraws
	CIntObject::showAll(to);
}

void CMenuScreen::activate()
{
	CIntObject::activate();
}

void CMenuScreen::switchToTab(size_t index)
{
	tabs->setActive(index);
}

void CMenuScreen::switchToTab(std::string name)
{
	switchToTab(vstd::find_pos(menuNameToEntry, name));
}

size_t CMenuScreen::getActiveTab() const
{
	return tabs->getActive();
}
void CMenuScreen::keyPressed(EShortcut key)
{
	if(key == EShortcut::ADVENTURE_OPEN_WIKI)
		ENGINE->windows().createAndPushWindow<WikiWindow>(WikiWindow::Style::BLUE);
}
//function for std::string -> std::function conversion for main menu
static std::function<void()> genCommand(CMenuScreen * menu, std::vector<std::string> menuType, const std::string & string)
{
	static const std::vector<std::string> commandType = {"to", "campaigns", "start", "load", "exit", "highscores", "wiki"};

	static const std::vector<std::string> gameType = {"single", "multi", "campaign", "tutorial", "battle"};

	std::list<std::string> commands;
	boost::split(commands, string, boost::is_any_of("\t "));

	if(!commands.empty())
	{
		size_t index = std::find(commandType.begin(), commandType.end(), commands.front()) - commandType.begin();
		commands.pop_front();
		if(index > 3 || !commands.empty())
		{
			switch(index)
			{
			case 0: //to - switch to another tab, if such tab exists
			{
				size_t index2 = std::find(menuType.begin(), menuType.end(), commands.front()) - menuType.begin();
				if(index2 != menuType.size())
					return std::bind((void(CMenuScreen::*)(size_t))&CMenuScreen::switchToTab, menu, index2);
				break;
			}
			case 1: //open campaign selection window
			{
				return std::bind(&CMainMenu::openCampaignScreen, GAME->mainmenu(), commands.front());
				break;
			}
			case 2: //start
			{
				switch(std::find(gameType.begin(), gameType.end(), commands.front()) - gameType.begin())
				{
			case 0:
				return []() { CMainMenu::openAuthenticatedArenaLobby(ELoadMode::SINGLE); };
			case 1:
				return []() { CMainMenu::openAuthenticatedArenaLobby(ELoadMode::MULTI); };
				case 2:
					return []() { CMainMenu::openLobby(ESelectionScreen::campaignList, true, {}, ELoadMode::NONE, false); };
				case 3:
					return []() { CMainMenu::startTutorial(); };
				case 4:
					return []() { CMainMenu::openLobby(ESelectionScreen::newGame, true, {}, ELoadMode::NONE, true); };
				}
				break;
			}
			case 3: //load
			{
				switch(std::find(gameType.begin(), gameType.end(), commands.front()) - gameType.begin())
				{
				case 0:
					return []() { CMainMenu::openLobby(ESelectionScreen::loadGame, true, {}, ELoadMode::SINGLE, false); };
				case 1:
					return []() { ENGINE->windows().createAndPushWindow<CMultiMode>(ESelectionScreen::loadGame); };
				case 2:
					return []() { CMainMenu::openLobby(ESelectionScreen::loadGame, true, {}, ELoadMode::CAMPAIGN, false); };
				case 3:
					return []() { CMainMenu::openLobby(ESelectionScreen::loadGame, true, {}, ELoadMode::TUTORIAL, false); };

				}
			}
			break;
			case 4: //exit
			{
				return []() { CInfoWindow::showYesNoDialog(LIBRARY->generaltexth->allTexts[69], std::vector<std::shared_ptr<CComponent>>(), [](){GAME->onShutdownRequested(false);}, 0, PlayerColor(1)); };
			}
			break;
			case 5: //highscores
			{
				return []() { CMainMenu::openHighScoreScreen(); };
			}
			case 6: //wiki
			{
				return []() { ENGINE->windows().createAndPushWindow<WikiWindow>(WikiWindow::Style::BLUE); };
			}
			}
		}
	}
	logGlobal->error("Failed to parse command: %s", string);
	return std::function<void()>();
}

std::shared_ptr<CButton> CMenuEntry::createButton(CMenuScreen * parent, const JsonNode & button)
{
	std::function<void()> command = genCommand(parent, parent->menuNameToEntry, button["command"].String());

	std::pair<std::string, std::string> help;
	if(!button["help"].isNull())
	{
		if(button["help"].isNumber() && button["help"].Float() > 0)
			help = LIBRARY->generaltexth->zelp[(size_t)button["help"].Float()];
		if(button["help"].isString() && !button["help"].String().empty())
			help = {"", LIBRARY->generaltexth->translate(button["help"].String())};
	}	

	Point point = adjustNegativeCoordinate(button["x"].Integer(), button["y"].Integer());
	EShortcut shortcut = ENGINE->shortcuts().findShortcut(button["shortcut"].String());

	if (shortcut == EShortcut::NONE && !button["shortcut"].String().empty())
		logGlobal->warn("Unknown shortcut '%s' found when loading main menu config!", button["shortcut"].String());

	auto result = std::make_shared<CButton>(point, AnimationPath::fromJson(button["name"]), help, command, shortcut);

	if (button["center"].Bool())
		result->moveBy(Point(-result->pos.w/2, -result->pos.h/2));

	return result;
}

CMenuEntry::CMenuEntry(CMenuScreen * parent, const JsonNode & config)
{
	OBJECT_CONSTRUCTION;
	setRedrawParent(true);
	pos = parent->pos;

	for(const JsonNode & node : config["images"].Vector())
		images.push_back(CMainMenu::createPicture(node));

	for (const JsonNode& node : config["buttons"].Vector())
	{
		auto tokens = node["command"].String().find(' ');
		std::pair<std::string, std::string> commandParts = {
			node["command"].String().substr(0, tokens),
			(tokens == std::string::npos) ? "" : node["command"].String().substr(tokens + 1)
		};

		if (commandParts.first == "campaigns")
		{
			const auto& campaign = CMainMenuConfig::get().getCampaigns()[commandParts.second];

			if (!campaign.isStruct())
			{
				logGlobal->warn("Campaign set %s not found", commandParts.second);
				continue;
			}

			bool fileExists = false;
			for (const auto& item : campaign["items"].Vector())
			{
				std::string filename = item["file"].String();

				if (CResourceHandler::get()->existsResource(ResourcePath(filename, EResType::CAMPAIGN)))
				{
					fileExists = true;
					break; 
				}
			}

			if (!fileExists)
			{
				logGlobal->warn("No valid files found for campaign set %s", commandParts.second);
				continue;
			}
		}

		buttons.push_back(createButton(parent, node));
		buttons.back()->setHoverable(true);
		buttons.back()->setRedrawParent(true);
	}
}

CMainMenuConfig::CMainMenuConfig()
	: campaignSets(JsonUtils::assembleFromFiles("config/campaignSets.json"))
	, config(JsonPath::builtin("config/mainmenu.json"))
{
	if (!config["scenario-selection"].isStruct())
		// Fallback for 1.6 mods
		if (config["game-select"].Vector().empty())
			handleFatalError("The main menu configuration file mainmenu.json is invalid or corrupted. Please check the file for errors, verify your mod setup, or reinstall VCMI to resolve the issue.", false);
}

const CMainMenuConfig & CMainMenuConfig::get()
{
	static const CMainMenuConfig config;
	return config;
}

const JsonNode & CMainMenuConfig::getConfig() const
{
	return config;
}

const JsonNode & CMainMenuConfig::getCampaigns() const
{
	return campaignSets;
}

CMainMenu::CMainMenu()
{
	pos.w = ENGINE->screenDimensions().x;
	pos.h = ENGINE->screenDimensions().y;

	menu = std::make_shared<CMenuScreen>(CMainMenuConfig::get().getConfig()["window"]);
	OBJECT_CONSTRUCTION;

	const auto& bgConfig = CMainMenuConfig::get().getConfig()["backgroundAround"];

	if (bgConfig.isString())
		backgroundAroundMenu = std::make_shared<CFilledTexture>(ImagePath::fromJson(bgConfig), pos);
	else
		backgroundAroundMenu = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), pos);
}

CMainMenu::~CMainMenu() = default;

void CMainMenu::playIntroVideos()
{
	auto playVideo = [](std::string video, bool rim, float scaleFactor, std::function<void(bool)> cb){
		if(ENGINE->video().open(VideoPath::builtin(video), scaleFactor))
			ENGINE->windows().createAndPushWindow<VideoWindow>(VideoPath::builtin(video), rim ? ImagePath::builtin("INTRORIM") : ImagePath::builtin(""), true, scaleFactor, [cb](bool skipped){ cb(skipped); });
		else
			cb(true);
	};

	playVideo("3DOLOGO.SMK", false, 1.25, [playVideo, this](bool skipped){
		if(!skipped)
			playVideo("NWCLOGO.SMK", false, 2, [playVideo, this](bool skipped){
				if(!skipped)
					playVideo("H3INTRO.SMK", true, 1, [this](bool skipped){
						playMusic();
					});
				else
					playMusic();
			});
		else
			playMusic();
	});
}

void CMainMenu::playMusic()
{
	ENGINE->music().playMusic(AudioPath::builtin("Music/MainMenu"), true, true);
}

void CMainMenu::activate()
{
	// check if screen was resized while main menu was inactive - e.g. in gameplay mode
	if (pos.dimensions() != ENGINE->screenDimensions())
		onScreenResize();

	CIntObject::activate();
}

void CMainMenu::onScreenResize()
{
	pos.w = ENGINE->screenDimensions().x;
	pos.h = ENGINE->screenDimensions().y;

	menu = nullptr;
	menu = std::make_shared<CMenuScreen>(CMainMenuConfig::get().getConfig()["window"]);

	backgroundAroundMenu->pos = pos;
}

void CMainMenu::makeActiveInterface()
{
	ENGINE->windows().pushWindow(GAME->mainmenu());
	ENGINE->windows().pushWindow(menu);
	menu->switchToTab(menu->getActiveTab());
}

void CMainMenu::openLobby(ESelectionScreen screenType, bool host, const std::vector<std::string> & names, ELoadMode loadMode, bool battleMode, bool hotseatMode, std::string server, ui16 port)
{
	GAME->server().resetStateForLobby(screenType == ESelectionScreen::newGame ? EStartMode::NEW_GAME : EStartMode::LOAD_GAME, screenType, EServerMode::LOCAL, names);
	GAME->server().loadMode = loadMode;
	GAME->server().hotseatMode = hotseatMode;
	GAME->server().battleMode = battleMode;

	ENGINE->windows().createAndPushWindow<CSimpleJoinScreen>(host, server, port);
}

void CMainMenu::openAuthenticatedArenaLobby(ELoadMode loadMode)
{
	if(!GAME->server().logicConnection)
	{
		logGlobal->error("Cannot enter authenticated arena lobby without a server connection.");
		return;
	}

	GAME->server().prepareStateForLobby(EStartMode::NEW_GAME, ESelectionScreen::newGame, EServerMode::LOCAL, {});
	GAME->server().loadMode = loadMode;
	GAME->server().battleMode = true;
	logGlobal->info("openAuthenticatedArenaLobby: loadMode=%d screenType=%d serverMode=%d hotseatMode=%d",
		static_cast<int>(GAME->server().loadMode),
		static_cast<int>(GAME->server().screenType),
		static_cast<int>(GAME->server().serverMode),
		static_cast<int>(GAME->server().hotseatMode));
	GAME->server().sendClientConnecting();
}

void CMainMenu::openCampaignLobby(const std::string & campaignFileName, std::string campaignSet)
{
	auto ourCampaign = CampaignHandler::getCampaign(campaignFileName);
	ourCampaign->campaignSet = campaignSet;
	openCampaignLobby(ourCampaign);
}

void CMainMenu::openCampaignLobby(std::shared_ptr<CampaignState> campaign)
{
	GAME->server().resetStateForLobby(EStartMode::CAMPAIGN, ESelectionScreen::campaignList, EServerMode::LOCAL, {});
	GAME->server().campaignStateToSend = campaign;
	ENGINE->windows().createAndPushWindow<CSimpleJoinScreen>();
}

void CMainMenu::openCampaignScreen(std::string name)
{
	auto const & config = CMainMenuConfig::get().getCampaigns();

	if(!vstd::contains(config.Struct(), name))
	{
		logGlobal->error("Unknown campaign set: %s", name);
		return;
	}

	ENGINE->windows().createAndPushWindow<CCampaignScreen>(config, name);
}

void CMainMenu::startTutorial()
{
	ResourcePath tutorialMap("Maps/Tutorial.tut", EResType::MAP);
	if(!CResourceHandler::get()->existsResource(tutorialMap))
	{
		CInfoWindow::showInfoDialog(LIBRARY->generaltexth->translate("core.genrltxt.742"), std::vector<std::shared_ptr<CComponent>>(), PlayerColor(1));
		return;
	}
		
	auto mapInfo = std::make_shared<CMapInfo>();
	mapInfo->mapInit(tutorialMap.getName());
	CMainMenu::openLobby(ESelectionScreen::newGame, true, {}, ELoadMode::NONE, false);
	GAME->server().startMapAfterConnection(mapInfo);
}

void CMainMenu::openHighScoreScreen()
{
	ENGINE->windows().createAndPushWindow<CHighScoreScreen>(CHighScoreScreen::HighScorePage::SCENARIO);
	return;
}

std::shared_ptr<CPicture> CMainMenu::createPicture(const JsonNode & config)
{
	return std::make_shared<CPicture>(ImagePath::fromJson(config["name"]), (int)config["x"].Float(), (int)config["y"].Float());
}

CMultiMode::CMultiMode(ESelectionScreen ScreenType)
	: screenType(ScreenType)
{
	OBJECT_CONSTRUCTION;

	background = std::make_shared<CPicture>(ImagePath::builtin("MuPopUpCustom"));
	pos = background->center(); //center, window has size of bg graphic

	const auto& multiplayerConfig = CMainMenuConfig::get().getConfig()["multiplayer"];
	if (multiplayerConfig.isVector() && !multiplayerConfig.Vector().empty())
		picture = std::make_shared<CPicture>(ImagePath::fromJson(*RandomGeneratorUtil::nextItem(multiplayerConfig.Vector(), CRandomGenerator::getDefault())), 16, 77);

	textTitle = std::make_shared<CTextBox>("", Rect(7, 18, 440, 50), 0, FONT_BIG, ETextAlignment::CENTER, Colors::WHITE);
	textTitle->setText(LIBRARY->generaltexth->zelp[263].second);

	textTitleIp = std::make_shared<CTextBox>("", Rect(7, 38, 440, 50), 0, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE);
	textTitleIp->setText(GAME->server().getRemoteHostname() + ":" + std::to_string(GAME->server().getRemotePort()));

	statusBar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 423, 440, 18), 7, 423));

	buttonHotseat = std::make_shared<CButton>(Point(373, 78 + 57 * 0), AnimationPath::builtin("MUBHOT.DEF"), LIBRARY->generaltexth->zelp[266], std::bind(&CMultiMode::hostTCP, this, EShortcut::MAIN_MENU_HOTSEAT), EShortcut::MAIN_MENU_HOTSEAT);
	buttonLobby = std::make_shared<CButton>(Point(373, 78 + 57 * 1), AnimationPath::builtin("MUBONL.DEF"), LIBRARY->generaltexth->zelp[265], std::bind(&CMultiMode::openLobby, this), EShortcut::MAIN_MENU_LOBBY);

	buttonHost = std::make_shared<CButton>(Point(373, 78 + 57 * 2), AnimationPath::builtin("MUBHOST.DEF"), CButton::tooltip(LIBRARY->generaltexth->translate("vcmi.mainMenu.hostTCP"), ""), std::bind(&CMultiMode::hostTCP, this, EShortcut::MAIN_MENU_HOST_GAME), EShortcut::MAIN_MENU_HOST_GAME);
	buttonJoin = std::make_shared<CButton>(Point(373, 78 + 57 * 3), AnimationPath::builtin("MUBJOIN.DEF"), CButton::tooltip(LIBRARY->generaltexth->translate("vcmi.mainMenu.joinTCP"), ""), std::bind(&CMultiMode::joinTCP, this, EShortcut::MAIN_MENU_JOIN_GAME), EShortcut::MAIN_MENU_JOIN_GAME);

	buttonCancel = std::make_shared<CButton>(Point(373, 380), AnimationPath::builtin("MUBCANC.DEF"), LIBRARY->generaltexth->zelp[288], [this](){ close();}, EShortcut::GLOBAL_CANCEL);
}

void CMultiMode::openLobby()
{
	close();
	GAME->server().getGlobalLobby().activateInterface();
}

void CMultiMode::hostTCP(EShortcut shortcut)
{
	auto savedScreenType = screenType;
	auto playerNames = getPlayersNames();

	// A TCP connection represents one local player. Extra configured names are
	// reserved for hotseat games and must not claim additional network colors.
	if(shortcut != EShortcut::MAIN_MENU_HOTSEAT)
		playerNames.resize(1);

	close();
	ENGINE->windows().createAndPushWindow<CMultiPlayers>(playerNames, savedScreenType, true, ELoadMode::MULTI, shortcut);
}

void CMultiMode::joinTCP(EShortcut shortcut)
{
	auto savedScreenType = screenType;
	auto playerNames = getPlayersNames();

	// See hostTCP: every remote client may configure only its own player.
	if(shortcut != EShortcut::MAIN_MENU_HOTSEAT)
		playerNames.resize(1);

	close();
	ENGINE->windows().createAndPushWindow<CMultiPlayers>(playerNames, savedScreenType, false, ELoadMode::MULTI, shortcut);
}

std::vector<std::string> CMultiMode::getPlayersNames()
{
	std::vector<std::string> playerNames;

	std::string playerNameStr = settings["session"]["playerNameOverride"].String();
	if(playerNameStr.empty())
		playerNameStr = settings["general"]["playerName"].String();
	if (playerNameStr == "Player")
		playerNameStr = LIBRARY->generaltexth->translate("core.genrltxt.434");
	playerNames.push_back(playerNameStr);

	for (const auto & playerName : settings["general"]["multiPlayerNames"].Vector())
	{
		const std::string &nameStr = playerName.String();
		if (!nameStr.empty())
		{
			playerNames.push_back(nameStr);
		}
	}

	return playerNames;
}

void CMultiMode::onNameChange(std::string newText)
{
	Settings name = settings.write["general"]["playerName"];
	name->String() = newText;
}

JoinScreen::JoinScreen(ESelectionScreen ScreenType, std::vector<std::string> PlayerNames) :
	screenType(ScreenType), playerNames(PlayerNames)
{
	OBJECT_CONSTRUCTION;

	background = std::make_shared<CPicture>(ImagePath::builtin("MuPopUpCustom"));
	pos = background->center(); //center, window has size of bg graphic

	textTitle = std::make_shared<CTextBox>("", Rect(7, 18, 440, 50), 0, FONT_BIG, ETextAlignment::CENTER, Colors::WHITE);
	textTitle->setText(LIBRARY->generaltexth->translate("vcmi.mainMenu.chooseAvailableServers"));

	statusBar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 423, 440, 18), 7, 423));

	buttonSearch = std::make_shared<CButton>(Point(373, 78 + 57 * 0), AnimationPath::builtin("MUBSRCH.DEF"), LIBRARY->generaltexth->zelp[273], [this](){
		auto savedScreenType = screenType;
		auto savedPlayerNames = playerNames;
		close();
		CMainMenu::openLobby(savedScreenType, false, savedPlayerNames, ELoadMode::MULTI, true);
	}, EShortcut::MAIN_MENU_JOIN_GAME);

	serverDiscovery = GAME->server().getNetworkHandler().createServerDiscovery(*this);
	serverDiscovery->start();

	buttonCancel = std::make_shared<CButton>(Point(373, 380), AnimationPath::builtin("MUBCANC.DEF"), LIBRARY->generaltexth->zelp[288], [this](){ close();}, EShortcut::GLOBAL_CANCEL);
}

JoinScreen::~JoinScreen()
{
	if(serverDiscovery)
		serverDiscovery->abort();
}

void JoinScreen::onServerDiscovered(const DiscoveredServer & server)
{
	ENGINE->dispatchMainThread([this, server]()
	{
		OBJECT_CONSTRUCTION;

		if(buttonsJoin.size() >= 12)
			return; //max 12 servers displayed

		auto button = std::make_shared<CButton>(Point(174, 114 + buttonsJoin.size() * 25), AnimationPath::builtin("GSPBUT2.DEF"), CButton::tooltip(), [this, server]{ 
			auto savedScreenType = screenType;
			auto savedPlayerNames = playerNames;
			close();
			CMainMenu::openLobby(savedScreenType, false, savedPlayerNames, ELoadMode::MULTI, true, false, server.address, server.port);
		});
		button->setTextOverlay(LIBRARY->generaltexth->translate("vcmi.mainMenu.join"), FONT_SMALL, Colors::WHITE);
		buttonsJoin.push_back(button);
		labelsJoin.push_back(std::make_shared<CLabel>(107, 124 + labelsJoin.size() * 25, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, server.address + ":" + std::to_string(server.port)));
		redraw();
	});
}

CMultiPlayers::CMultiPlayers(const std::vector<std::string>& playerNames, ESelectionScreen ScreenType, bool Host, ELoadMode LoadMode, EShortcut shortcut)
	: host(Host), hotseat(shortcut == EShortcut::MAIN_MENU_HOTSEAT), loadMode(LoadMode), screenType(ScreenType)
{
	OBJECT_CONSTRUCTION;
	background = std::make_shared<CPicture>(ImagePath::builtin("MUHOTSEA.bmp"));
	pos = background->center(); //center, window has size of bg graphic

	std::string textTitleValue;
	std::string textSubtitleValue;
	std::string hotseatText = LIBRARY->generaltexth->allTexts[446];
	std::vector<std::string> hotseatLines;
	boost::split(hotseatLines, hotseatText, boost::is_any_of("\n"));
	if(hotseatLines.size() > 1)
	{
		textSubtitleValue = hotseatLines[1];
	}

	switch (shortcut)
	{
	case EShortcut::MAIN_MENU_HOTSEAT:
		textTitleValue = hotseatLines.empty() ? hotseatText : hotseatLines.front();
		break;
	case EShortcut::MAIN_MENU_HOST_GAME:
		textTitleValue = LIBRARY->generaltexth->translate("vcmi.mainMenu.hostTCP");
		break;
	case EShortcut::MAIN_MENU_JOIN_GAME:
		textTitleValue = LIBRARY->generaltexth->translate("vcmi.mainMenu.joinTCP");
		break;
	}

	textTitle = std::make_shared<CTextBox>(textTitleValue, Rect(25, 10, 315, 30), 0, FONT_BIG, ETextAlignment::CENTER, Colors::WHITE);
	textSubtitle = std::make_shared<CTextBox>(textSubtitleValue, Rect(25, 40, 315, 35), 0, FONT_BIG, ETextAlignment::CENTER, Colors::WHITE);

	for(int i = 0; i < inputNames.size(); i++)
	{
		inputNames[i] = std::make_shared<CTextInput>(Rect(60, 85 + i * 30, 280, 16), background->getSurface());
		inputNames[i]->setCallback(std::bind(&CMultiPlayers::onChange, this, _1));
	}

	buttonOk = std::make_shared<CButton>(Point(95, 338), AnimationPath::builtin("MUBCHCK.DEF"), LIBRARY->generaltexth->zelp[560], std::bind(&CMultiPlayers::enterSelectionScreen, this), EShortcut::GLOBAL_ACCEPT);
	buttonCancel = std::make_shared<CButton>(Point(205, 338), AnimationPath::builtin("MUBCANC.DEF"), LIBRARY->generaltexth->zelp[561], [this](){ close();}, EShortcut::GLOBAL_CANCEL);
	statusBar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 381, 348, 18), 7, 381));

	for(int i = 0; i < playerNames.size(); i++)
	{
		inputNames[i]->setText(playerNames[i]);
	}

	buttonOk->block(hotseat && countEnteredNames() < 2);
#ifndef VCMI_MOBILE
	inputNames[0]->giveFocus();
#endif
}

size_t CMultiPlayers::countEnteredNames() const
{
	return std::count_if(inputNames.begin(), inputNames.end(), [](const auto & playerName)
	{
		return playerName->getText().length();
	});
}

void CMultiPlayers::onChange(std::string newText)
{
	buttonOk->block(hotseat && countEnteredNames() < 2);
}

void CMultiPlayers::enterSelectionScreen()
{
	std::vector<std::string> playerNames;
	for(auto playerName : inputNames)
	{
		if (playerName->getText().length())
			playerNames.push_back(playerName->getText());
	}

	Settings playerName = settings.write["general"]["playerName"];
	Settings multiPlayerNames = settings.write["general"]["multiPlayerNames"];
	multiPlayerNames->Vector().clear();
	if (!playerNames.empty())
	{
		playerName->String() = playerNames.front();
		for (auto playerNameIt = playerNames.begin()+1; playerNameIt != playerNames.end(); playerNameIt++)
		{
			multiPlayerNames->Vector().push_back(JsonNode(*playerNameIt));
		}
	}
	else
	{
		// Without the check the saving crashes directly.
		// When empty reset the player's name. This would translate to it being
		// the default for the next run. But enables deleting players, by just
		// deleting the names, otherwise some UI element should have been added.
		playerName->clear();
	}

	if(hotseat && playerNames.size() < 2)
		return;

	if(!host)
	{
		auto savedScreenType = screenType;
		auto savedPlayerNames = playerNames;
		close();
		ENGINE->windows().createAndPushWindow<JoinScreen>(savedScreenType, savedPlayerNames);
		return;
	}
	CMainMenu::openLobby(screenType, host, playerNames, loadMode, true, hotseat);
}

CSimpleJoinScreen::CSimpleJoinScreen(bool host, std::string server, ui16 port)
{
	OBJECT_CONSTRUCTION;
	background = std::make_shared<CPicture>(ImagePath::builtin("MUDIALOG.bmp")); // address background
	pos = background->center(); //center, window has size of bg graphic (x,y = 396,278 w=232 h=212)

	textTitle = std::make_shared<CTextBox>("", Rect(20, 10, 205, 50), 0, FONT_BIG, ETextAlignment::CENTER, Colors::WHITE);
	inputAddress = std::make_shared<CTextInput>(Rect(25, 68, 175, 16), background->getSurface());
	inputPort = std::make_shared<CTextInput>(Rect(25, 115, 175, 16), background->getSurface());
	buttonOk = std::make_shared<CButton>(Point(26, 142), AnimationPath::builtin("MUBCHCK.DEF"), LIBRARY->generaltexth->zelp[560], std::bind(&CSimpleJoinScreen::connectToServer, this), EShortcut::GLOBAL_ACCEPT);
	inputAddress->setText(GAME->server().getRemoteHostname());
	inputPort->setText(std::to_string(GAME->server().getRemotePort()));

	if(host && !inputAddress->getText().empty())
	{
		textTitle->setText(LIBRARY->generaltexth->translate("vcmi.mainMenu.serverConnecting"));
		buttonOk->block(true);
		startConnection(inputAddress->getText(), boost::lexical_cast<ui16>(inputPort->getText()));
	}
	else
	{
		textTitle->setText(LIBRARY->generaltexth->translate("vcmi.mainMenu.serverAddressEnter"));
		inputAddress->setCallback(std::bind(&CSimpleJoinScreen::onChange, this, _1));
		inputPort->setCallback(std::bind(&CSimpleJoinScreen::onChange, this, _1));
		inputPort->setFilterNumber(0, 65535);
		inputAddress->giveFocus();
	}
	buttonOk->block(inputAddress->getText().empty() || inputPort->getText().empty());

	buttonCancel = std::make_shared<CButton>(Point(142, 142), AnimationPath::builtin("MUBCANC.DEF"), LIBRARY->generaltexth->zelp[561], std::bind(&CSimpleJoinScreen::leaveScreen, this), EShortcut::GLOBAL_CANCEL);
	statusBar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 186, 218, 18), 7, 186));

	if(!server.empty())
	{
		inputAddress->setText(server);
		inputPort->setText(std::to_string(port));
		connectToServer();
	}
}

void CSimpleJoinScreen::connectToServer()
{
	textTitle->setText(LIBRARY->generaltexth->translate("vcmi.mainMenu.serverConnecting"));
	buttonOk->block(true);
	ENGINE->input().stopTextInput();

	startConnection(inputAddress->getText(), boost::lexical_cast<ui16>(inputPort->getText()));
}

void CSimpleJoinScreen::leaveScreen()
{
	textTitle->setText(LIBRARY->generaltexth->translate("vcmi.mainMenu.serverClosing"));
	GAME->server().setState(EClientState::CONNECTION_CANCELLED);
	close();
}

void CSimpleJoinScreen::onChange(const std::string & newText)
{
	buttonOk->block(inputAddress->getText().empty() || inputPort->getText().empty());
}

void CSimpleJoinScreen::startConnection(const std::string & addr, ui16 port)
{
	GAME->server().connectToServer(addr, port);
}

CLoginScreen::CLoginScreen()
{
	OBJECT_CONSTRUCTION;
	try
	{
		const JsonNode config(JsonPath::builtin("config/arenaServers.json"));
		for(const auto & entry : config["servers"].Vector())
		{
			const auto port = entry["port"].Integer();
			if(entry["id"].String().empty() || entry["name"].String().empty() || entry["hostname"].String().empty() || port < 1 || port > 65535)
				continue;
			if(std::any_of(serverChoices.begin(), serverChoices.end(), [&](const auto & choice){ return choice.id == entry["id"].String(); }))
				continue;
			serverChoices.push_back({entry["id"].String(), entry["name"].String(), entry["hostname"].String(), static_cast<int>(port), entry["local"].Bool()});
		}
	}
	catch(const std::exception & error)
	{
		logGlobal->warn("Unable to load arena server list: %s", error.what());
	}
	if(serverChoices.empty())
	{
		serverChoices.push_back({"local", LIBRARY->generaltexth->translate("vcmi.arena.login.localServer"), settings["server"]["localHostname"].String(), static_cast<int>(settings["server"]["localPort"].Integer()), true});
		serverChoices.push_back({"remote", LIBRARY->generaltexth->translate("vcmi.arena.login.remoteServer"), settings["server"]["arenaRemoteHostname"].String(), static_cast<int>(settings["server"]["arenaRemotePort"].Integer()), false});
	}
	const int offset = 0;
	background = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), Rect(0, 0, 380, 346 + offset));
	pos = background->center();
	title = std::make_shared<CTextBox>(LIBRARY->generaltexth->translate("vcmi.arena.login.enterCredentials"), Rect(30, 20, 320, 24), 0, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE);
	rememberPasswordLabel = std::make_shared<CLabel>(75, 105 + offset, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.arena.login.rememberPassword"));
	usernameLabel = std::make_shared<CLabel>(50, 142 + offset, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.arena.login.username"));
	passwordLabel = std::make_shared<CLabel>(50, 205 + offset, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.arena.login.password"));
	usernameMarker = std::make_shared<CLabel>(50, 164 + offset, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "★");
	passwordMarker = std::make_shared<CLabel>(50, 227 + offset, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "★");
	username = std::make_shared<CTextInput>(Rect(68, 160 + offset, 262, 20), FONT_SMALL, ETextAlignment::CENTERLEFT, false);
	password = std::make_shared<CTextInput>(Rect(68, 223 + offset, 262, 20), FONT_SMALL, ETextAlignment::CENTERLEFT, false);
	username->setColor(Colors::WHITE);
	password->setColor(Colors::WHITE);
	const JsonNode dropdownConfig(JsonPath::builtin("config/widgets/arenaServerDropdown.json"));
	serverDropdown = std::make_shared<ComboBox>(Point(65, 64), AnimationPath::builtin("lobby/dropdown"), CButton::tooltip(), dropdownConfig, Point(0, 24));
	serverDropdown->setImageOrder(0, 1, 0, 0);
	serverDropdown->setTextOverlay("", FONT_SMALL, Colors::WHITE);
	serverDropdown->onConstructItems = [this](std::vector<const void *> & items)
	{
		for(const auto & choice : serverChoices)
			items.push_back(&choice);
	};
	serverDropdown->getItemText = [](int, const void * item)
	{
		return item ? static_cast<const ServerChoice *>(item)->name : std::string();
	};
	serverDropdown->onSetItem = [this](const void * item)
	{
		if(item)
			selectServerTarget(static_cast<int>(static_cast<const ServerChoice *>(item) - serverChoices.data()));
	};
	int selected = 0;
	const auto savedId = settings["server"]["arenaServerId"].String();
	const bool wasRemote = settings["server"]["arenaServerTarget"].String() == "remote";
	const auto previousHost = settings["server"][wasRemote ? "arenaRemoteHostname" : "localHostname"].String();
	const auto previousPort = settings["server"][wasRemote ? "arenaRemotePort" : "localPort"].Integer();
	for(int i = 0; i < static_cast<int>(serverChoices.size()); ++i)
	{
		const auto & choice = serverChoices[i];
		if(choice.hostname == previousHost && choice.port == previousPort)
			selected = i;
	}
	for(int i = 0; i < static_cast<int>(serverChoices.size()); ++i)
		if(serverChoices[i].id == savedId)
			selected = i;
	serverDropdown->setItem(selected);
	const JsonNode savedLogin = loadSavedLoginProfile();
	const bool usesLoginProfile = !savedLoginProfilePath().empty();
	const bool remembersLogin = usesLoginProfile ? savedLogin["remember"].Bool() : settings["general"]["rememberLogin"].Bool();
	username->setText(usesLoginProfile ? savedLogin["username"].String() : settings["general"]["lastLoginName"].String());
	if(remembersLogin)
		password->setText(usesLoginProfile ? savedLogin["password"].String() : settings["general"]["lastLoginPassword"].String());
	username->setCallback([this](const std::string &){ updateButton(); });
	password->setCallback([this](const std::string &){ updateButton(); });
	rememberPassword = std::make_shared<CToggleButton>(Point(50, 101 + offset), AnimationPath::builtin("lobby/checkboxSmall"), CButton::tooltip(), [this](bool remember){
		if(!savedLoginProfilePath().empty())
		{
			// Persist the actual password only after the server accepts the login.
			saveLoginProfile(username->getText(), "", remember);
			redraw();
			return;
		}
		Settings rememberLogin = settings.write["general"]["rememberLogin"];
		rememberLogin->Bool() = remember;
		if(!remember)
		{
			Settings savedPassword = settings.write["general"]["lastLoginPassword"];
			savedPassword->String() = "";
		}
		redraw();
	});
	rememberPassword->setSelectedSilent(remembersLogin);
	buttonLogin = std::make_shared<CButton>(Point(152, 280 + offset), AnimationPath::builtin("MUBCHCK.DEF"), CButton::tooltip(), [this](){ submit(); }, EShortcut::GLOBAL_ACCEPT);
	updateButton();
	if(GAME->server().getRemoteHostname().empty())
	{
		title->setText("Server address is not configured.");
		return;
	}
	if(settings["session"]["autoArenaMulti"].Bool() && !username->getText().empty() && !password->getText().empty())
	{
		pendingLogin = true;
		submit();
	}
}

void CLoginScreen::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	CMessage::drawBorder(PlayerColor(1), to, pos.w + 28, pos.h + 29, pos.x - 14, pos.y - 15);
}

void CLoginScreen::selectServerTarget(int target)
{
	if(target < 0 || target >= static_cast<int>(serverChoices.size()))
		return;
	const auto & choice = serverChoices[target];
	Settings selectedId = settings.write["server"]["arenaServerId"];
	selectedId->String() = choice.id;
	Settings selectedTarget = settings.write["server"]["arenaServerTarget"];
	selectedTarget->String() = choice.local ? "local" : "remote";
	Settings selectedHostname = settings.write["server"]["remoteHostname"];
	selectedHostname->String() = choice.hostname;
	Settings selectedPort = settings.write["server"]["remotePort"];
	selectedPort->Integer() = choice.port;
	Settings endpointHostname = settings.write["server"][choice.local ? "localHostname" : "arenaRemoteHostname"];
	endpointHostname->String() = choice.hostname;
	Settings endpointPort = settings.write["server"][choice.local ? "localPort" : "arenaRemotePort"];
	endpointPort->Integer() = choice.port;
	if(buttonLogin)
		updateButton();
	// Refresh the selected endpoint before accepting login input.
	redraw();
}

void CLoginScreen::blockServerTarget(bool block)
{
	serverDropdown->block(block);
}

void CLoginScreen::updateButton()
{
	buttonLogin->block(GAME->server().getRemoteHostname().empty() || username->getText().empty() || password->getText().empty() || GAME->server().getState() == EClientState::CONNECTING);
}

void CLoginScreen::submit()
{
	username->commitComposedText();
	password->commitComposedText();

	if(username->getText().empty() || password->getText().empty())
		return;
	if(GAME->server().getRemoteHostname().empty())
		return;
	pendingLogin = true;
	if(GAME->server().getState() != EClientState::AUTHENTICATING)
	{
		title->setText(LIBRARY->generaltexth->translate("vcmi.arena.login.connecting"));
		blockServerTarget(true);
		GAME->server().connectToServer(GAME->server().getRemoteHostname(), GAME->server().getRemotePort());
		updateButton();
		return;
	}
	if(GAME->server().getState() == EClientState::AUTHENTICATING)
	{
		pendingLogin = false;
		title->setText(LIBRARY->generaltexth->translate("vcmi.arena.login.authenticating"));
		buttonLogin->block(true);
		GAME->server().sendAuthentication(username->getText(), password->getText());
	}
}

void CLoginScreen::onConnected()
{
	title->setText(LIBRARY->generaltexth->translate("vcmi.arena.login.enterCredentials"));
	updateButton();
	if(pendingLogin)
		submit();
}

void CLoginScreen::onAuthenticationFailed(const std::string & reason)
{
	pendingLogin = false;
	blockServerTarget(false);
	password->setText("");
	title->setText(reason);
	updateButton();
	password->giveFocus();
}

void CLoginScreen::onAuthenticationAccepted()
{
	if(!savedLoginProfilePath().empty())
	{
		saveLoginProfile(username->getText(), password->getText(), rememberPassword->isSelected());
		password->setText("");
		close();
		GAME->mainmenu()->makeActiveInterface();
		GAME->mainmenu()->playMusic();
		if(settings["session"]["autoArenaMulti"].Bool())
			CMainMenu::openAuthenticatedArenaLobby(ELoadMode::MULTI);
		return;
	}
	Settings loginName = settings.write["general"]["lastLoginName"];
	loginName->String() = username->getText();
	Settings loginPassword = settings.write["general"]["lastLoginPassword"];
	loginPassword->String() = rememberPassword->isSelected() ? password->getText() : "";
	password->setText("");
	close();
	GAME->mainmenu()->makeActiveInterface();
	GAME->mainmenu()->playMusic();
	if(settings["session"]["autoArenaMulti"].Bool())
		CMainMenu::openAuthenticatedArenaLobby(ELoadMode::MULTI);
}

CLoadingScreen::CLoadingScreen()
	: CLoadingScreen(getBackground())
{
}

CLoadingScreen::CLoadingScreen(ImagePath background)
	: CWindowObject(BORDERED, background)
{
	OBJECT_CONSTRUCTION;

	addUsedEvents(TIME);

	ENGINE->music().stopMusic(5000);

	const auto& conf = CMainMenuConfig::get().getConfig()["loading"];

	const auto& backgroundConfig = conf["background"];
	if (!backgroundConfig.Vector().empty())
		backimg = std::make_shared<CPicture>(ImagePath::fromJson(*RandomGeneratorUtil::nextItem(backgroundConfig.Vector(), CRandomGenerator::getDefault())));

	for (const JsonNode& node : conf["images"].Vector())
	{
		auto image = std::make_shared<CPicture>(ImagePath::fromJson(*RandomGeneratorUtil::nextItem(node["name"].Vector(), CRandomGenerator::getDefault())), Point(node["x"].Integer(), node["y"].Integer()));
		images.push_back(image);
	}

	const auto& loadframeConfig = conf["loadframe"];
	if (loadframeConfig.isStruct())
	{
		loadFrame = std::make_shared<CPicture>(
			ImagePath::fromJson(*RandomGeneratorUtil::nextItem(loadframeConfig["name"].Vector(), CRandomGenerator::getDefault())),
			loadframeConfig["x"].Integer(),
			loadframeConfig["y"].Integer()
			);
	}

	const auto& loadbarConfig = conf["loadbar"];
	if (loadbarConfig.isStruct())
	{
		AnimationPath loadbarPath = AnimationPath::fromJson(*RandomGeneratorUtil::nextItem(loadbarConfig["name"].Vector(), CRandomGenerator::getDefault()));
		const int posx = loadbarConfig["x"].Integer();
		const int posy = loadbarConfig["y"].Integer();
		const int blockSize = loadbarConfig["size"].Integer();
		const int blocksAmount = loadbarConfig["amount"].Integer();
		for (int i = 0; i < blocksAmount; ++i) 
		{
			progressBlocks.push_back(std::make_shared<CAnimImage>(loadbarPath, i, 0, posx + i * blockSize, posy));
			progressBlocks.back()->deactivate();
			progressBlocks.back()->visible = false;
		}
	}
}

CLoadingScreen::~CLoadingScreen()
{
}

void CLoadingScreen::tick(uint32_t msPassed)
{
	if(!progressBlocks.empty())
	{
		int status = float(get()) / 255.f * progressBlocks.size();
		
		for(int i = 0; i < status; ++i)
		{
			progressBlocks.at(i)->activate();
			progressBlocks.at(i)->visible = true;
		}
	}
}

ImagePath CLoadingScreen::getBackground()
{
	ImagePath fname = ImagePath::builtin("loadbar");
	const auto & conf = CMainMenuConfig::get().getConfig()["loading"];

	if(conf.isStruct())
	{
		if(conf["background"].isVector())
			return ImagePath::fromJson(*RandomGeneratorUtil::nextItem(conf["background"].Vector(), CRandomGenerator::getDefault()));
		
		if(conf["background"].isString())
			return ImagePath::fromJson(conf["background"]);
		
		return fname;
	}
	
	if(conf.isVector() && !conf.Vector().empty())
		return ImagePath::fromJson(*RandomGeneratorUtil::nextItem(conf.Vector(), CRandomGenerator::getDefault()));
	
	return fname;
}
