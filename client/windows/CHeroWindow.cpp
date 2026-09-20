/*
 * CHeroWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "wiki/WikiWindow.h"

#include "CCreatureWindow.h"
#include "CHeroBackpackWindow.h"
#include "CKingdomInterface.h"
#include "CExchangeWindow.h"

#include "../CPlayerInterface.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/TextAlignment.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/CComponent.h"
#include "../widgets/CGarrisonInt.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "../widgets/Buttons.h"
#include "../widgets/Slider.h"
#include "../render/IRenderHandler.h"

#include "../lib/CConfigHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/CSkillHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/callback/CCallback.h"
#include "../lib/entities/artifact/ArtifactUtils.h"
#include "../lib/entities/hero/CHeroHandler.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapping/CMapHeader.h"
#include "../lib/networkPacks/ArtifactLocation.h"
#include "../lib/texts/CGeneralTextHandler.h"

void CHeroSwitcher::clickPressed(const Point & cursorPosition)
{
	//TODO: do not recreate window
	if (false)
	{
		owner->updateArtifacts();
	}
	else
	{
		const CGHeroInstance * buf = hero;
		ENGINE->windows().popWindows(1);
		ENGINE->windows().createAndPushWindow<CHeroWindow>(buf);
	}
}

CHeroSwitcher::CHeroSwitcher(CHeroWindow * owner_, Point pos_, const CGHeroInstance * hero_)
	: CIntObject(LCLICK),
	owner(owner_),
	hero(hero_)
{
	OBJECT_CONSTRUCTION;
	pos += pos_;

	image = std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsSmall"), hero->getIconIndex());
	pos.w = image->pos.w;
	pos.h = image->pos.h;

	if(GAME->interface()->cb->getMapHeader()->battleOnly)
	{
		auto star = std::make_shared<GraphicalPrimitiveCanvas>(Rect(2, 1, 21, 20));
		const std::array<Point, 10> points = {
			Point(10, 0), Point(13, 7), Point(20, 7), Point(15, 11), Point(17, 19),
			Point(10, 14), Point(3, 19), Point(5, 11), Point(0, 7), Point(7, 7)
		};
		// Fill the concave star one pixel at a time, then draw its black outline.
		// This keeps the marker readable on both light and dark hero portraits.
		for(int y = 0; y < 20; ++y)
		{
			for(int x = 0; x < 21; ++x)
			{
				const double sampleX = x + 0.5;
				const double sampleY = y + 0.5;
				bool inside = false;
				for(size_t current = 0, previous = points.size() - 1; current < points.size(); previous = current++)
				{
					const auto & first = points[current];
					const auto & second = points[previous];
					if((first.y > sampleY) != (second.y > sampleY)
						&& sampleX < (second.x - first.x) * (sampleY - first.y) / (second.y - first.y) + first.x)
						inside = !inside;
				}
				if(inside)
					star->addBox(Point(x, y), Point(1, 1), Colors::YELLOW);
			}
		}
		for(size_t index = 0; index < points.size(); ++index)
		{
			const auto & from = points[index];
			const auto & to = points[(index + 1) % points.size()];
			star->addLine(from, to, Colors::BLACK);
		}
		mainHeroMarker = star;
		refreshArenaMainHeroMarker();
	}
}

void CHeroSwitcher::refreshArenaMainHeroMarker()
{
	if(!mainHeroMarker)
		return;
	const auto * playerState = GAME->interface()->cb->getPlayerState(hero->tempOwner, false);
	const bool isMainHero = playerState && playerState->arenaMainHero == hero->id;
	if(mainHeroMarker->isDisabled() == isMainHero)
		mainHeroMarker->setEnabled(isMainHero);
}

CHeroWindow::CHeroWindow(const CGHeroInstance * hero, bool readOnly_, std::function<void()> closeCallback_)
	: CWindowObject(PLAYER_COLORED, ImagePath::builtin("HeroScr4"))
	, readOnly(readOnly_)
	, closeCallback(std::move(closeCallback_))
{
	auto & heroscrn = LIBRARY->generaltexth->heroscrn;

	OBJECT_CONSTRUCTION;
	curHero = hero;

	if(readOnly)
		background->setPlayerColor(hero->tempOwner);
	banner = std::make_shared<CAnimImage>(AnimationPath::builtin("CREST58"), (readOnly ? hero->tempOwner : GAME->interface()->playerID).getNum(), 0, 606, 8);
	name = std::make_shared<CLabel>(190, 38, EFonts::FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW);
	title = std::make_shared<CLabel>(190, 65, EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE);

	statusbar = CGStatusBar::create(std::make_shared<CPicture>(background->getSurface(), Rect(7, 559, 660, 19), 7, 559));

	quitButton = std::make_shared<CButton>(Point(609, 516), AnimationPath::builtin("hsbtns.def"), CButton::tooltip(heroscrn[17]), [this]()
	{
		if(readOnly && closeCallback)
			closeCallback();
		else
			close();
	}, EShortcut::GLOBAL_RETURN);

	const bool arenaMode = GAME->interface()->cb->getMapHeader()->battleOnly;
	auto questAction = [this, arenaMode]()
	{
		if(arenaMode)
			GAME->interface()->requestArenaMainHero(curHero);
		else
			GAME->interface()->showQuestLog();
	};
	auto questTooltip = arenaMode
		? CButton::tooltipLocalized("vcmi.arena.hero.setMain")
		: CButton::tooltip(heroscrn[0]);

	if(readOnly)
	{
		// Read-only battle inspection has no mutation or navigation controls.
	}
	else if(settings["general"]["enableUiEnhancements"].Bool())
	{
		questlogButton = std::make_shared<CButton>(Point(314, 429), AnimationPath::builtin("hsbtns4.def"), questTooltip, questAction, EShortcut::ADVENTURE_QUEST_LOG);
		backpackButton = std::make_shared<CButton>(Point(424, 429), AnimationPath::builtin("heroBackpack"), CButton::tooltipLocalized("vcmi.heroWindow.openBackpack"), [this](){ createBackpackWindow(); }, EShortcut::HERO_BACKPACK);
		backpackButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("heroWindow/backpackButtonIcon")));
		dismissButton = std::make_shared<CButton>(Point(534, 429), AnimationPath::builtin("hsbtns2.def"), CButton::tooltip(heroscrn[28]), [this](){ dismissCurrent(); }, EShortcut::HERO_DISMISS);
	}
	else
	{
		questlogLabel = std::make_shared<CTextBox>(arenaMode ? LIBRARY->generaltexth->translate("vcmi.arena.hero.setMain.hover") : LIBRARY->generaltexth->jktexts[8], Rect(370, 430, 65, 35), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
		dismissLabel = std::make_shared<CTextBox>(arenaMode ? LIBRARY->generaltexth->translate("vcmi.radialWheel.heroDismiss") : LIBRARY->generaltexth->jktexts[9], Rect(510, 430, 65, 35), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
		dismissButton = std::make_shared<CButton>(Point(454, 429), AnimationPath::builtin("hsbtns2.def"), CButton::tooltip(heroscrn[28]), [this](){ dismissCurrent(); }, EShortcut::HERO_DISMISS);
		questlogButton = std::make_shared<CButton>(Point(314, 429), AnimationPath::builtin("hsbtns4.def"), questTooltip, questAction, EShortcut::ADVENTURE_QUEST_LOG);
	}

	if(arenaMode && questlogButton)
	{
		questlogButton->setTextOverlay(LIBRARY->generaltexth->translate("vcmi.arena.hero.setMain.button"), FONT_SMALL, Colors::YELLOW);
		const auto * playerState = GAME->interface()->cb->getPlayerState(hero->tempOwner, false);
		questlogButton->block(playerState && playerState->arenaMainHero == hero->id);
	}

	if(!readOnly)
	{
		formations = std::make_shared<CToggleGroup>(0);
		formations->addToggle(0, std::make_shared<CToggleButton>(Point(481, 483), AnimationPath::builtin("hsbtns6.def"), std::make_pair(heroscrn[23], heroscrn[29]), 0, EShortcut::HERO_TIGHT_FORMATION));
		formations->addToggle(1, std::make_shared<CToggleButton>(Point(481, 519), AnimationPath::builtin("hsbtns7.def"), std::make_pair(heroscrn[24], heroscrn[30]), 0, EShortcut::HERO_LOOSE_FORMATION));
	}

	if(!readOnly && hero->getCommander())
	{
		commanderButton = std::make_shared<CButton>(Point(317, 18), AnimationPath::builtin("heroCommander"), CButton::tooltipLocalized("vcmi.heroWindow.openCommander"), [&](){ commanderWindow(); }, EShortcut::HERO_COMMANDER);
		commanderButton->setOverlay(std::make_shared<CPicture>(ImagePath::builtin("heroWindow/commanderButtonIcon")));
	}

	//right list of heroes
	if(!readOnly)
		for(int i=0; i < std::min(GAME->interface()->cb->howManyHeroes(false), 8); i++)
			heroList.push_back(std::make_shared<CHeroSwitcher>(this, Point(612, 87 + i * 54), GAME->interface()->cb->getHeroBySerial(i, false)));

	//areas
	portraitArea = std::make_shared<LRClickableAreaWText>(Rect(18, 18, 58, 64));
	portraitImage = std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsLarge"), 0, 0, 19, 19);

	if(!readOnly)
		portraitWikiArea = std::make_shared<LRClickableArea>(Rect(18, 18, 58, 64), [this]()
		{
			ENGINE->windows().createAndPushWindow<WikiWindow>(
				WikiWindow::Style::BROWN,
				WikiEntryKey{WikiCategory::HERO, curHero->getHeroType()->getJsonKey()});
		});

	for(int v = 0; v < GameConstants::PRIMARY_SKILLS; ++v)
	{
		auto area = std::make_shared<LRClickableAreaWTextComp>(Rect(30 + 70 * v, 109, 42, 64), ComponentType::PRIM_SKILL);
		area->text = LIBRARY->generaltexth->arraytxt[2+v];
		area->component.subType = PrimarySkill(v);
		area->hoverText = boost::str(boost::format(LIBRARY->generaltexth->heroscrn[1]) % LIBRARY->generaltexth->primarySkillNames[v]);
		primSkillAreas.push_back(area);

		auto value = std::make_shared<CLabel>(53 + 70 * v, 166, FONT_SMALL, ETextAlignment::CENTER);
		primSkillValues.push_back(value);
	}

	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 0, 0, 32, 111));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 1, 0, 102, 111));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 2, 0, 172, 111));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 3, 0, 162, 230));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 4, 0, 20, 230));
	primSkillImages.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 5, 0, 242, 111));

	specImage = std::make_shared<CAnimImage>(AnimationPath::builtin("UN44"), 0, 0, 18, 180);
	specArea = std::make_shared<LRClickableAreaWText>(Rect(18, 180, 136, 42), LIBRARY->generaltexth->heroscrn[27]);
	specName = std::make_shared<CLabel>(69, 205);

	expArea = std::make_shared<LRClickableAreaWText>(Rect(18, 228, 136, 42), LIBRARY->generaltexth->heroscrn[9]);
	morale = std::make_shared<MoraleLuckBox>(true, Rect(175, 179, 53, 45));
	luck = std::make_shared<MoraleLuckBox>(false, Rect(233, 179, 53, 45));
	spellPointsArea = std::make_shared<LRClickableAreaWText>(Rect(162,228, 136, 42), LIBRARY->generaltexth->heroscrn[22]);

	expValue = std::make_shared<CLabel>(68, 252);
	manaValue = std::make_shared<CLabel>(211, 252);

	if(hero->secSkills.size() > 8)
	{
		auto divisionRoundUp = [](int x, int y){ return (x + (y - 1)) / y; };
		int lines = divisionRoundUp(hero->secSkills.size(), 2);
		secSkillSlider = std::make_shared<CSlider>(Point(284, 276), 189, [this](int val){ CHeroWindow::updateArtifacts(); }, 4, lines, 0, Orientation::VERTICAL, CSlider::BROWN);
		secSkillSlider->setPanningStep(48);
		secSkillSlider->setScrollBounds(Rect(-266, 0, secSkillSlider->pos.x - pos.x + secSkillSlider->pos.w, secSkillSlider->pos.h));
	}

	for(int i = 0; i < std::min<size_t>(hero->secSkills.size(), 8u); ++i)
	{
		bool isSmallBox = (secSkillSlider && i%2 == 1);
		Rect r(i%2 == 0  ?  18  :  162,  276 + 48 * (i/2), isSmallBox ? 120 : 136,  42);
		secSkills.emplace_back(std::make_shared<CSecSkillPlace>(r.topLeft(), CSecSkillPlace::ImageSize::MEDIUM));

		int x = (i % 2) ? 212 : 68;
		int y = 280 + 48 * (i/2);
		int width = isSmallBox ? 71 : 87;

		secSkillValues.push_back(std::make_shared<CLabel>(x, y, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", width));
		secSkillNames.push_back(std::make_shared<CLabel>(x, y+20, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "", width));
	}

	// various texts
	labels.push_back(std::make_shared<CLabel>(52, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->jktexts[1]));
	labels.push_back(std::make_shared<CLabel>(123, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->jktexts[2]));
	labels.push_back(std::make_shared<CLabel>(193, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->jktexts[3]));
	labels.push_back(std::make_shared<CLabel>(262, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->jktexts[4]));

	labels.push_back(std::make_shared<CLabel>(69, 183, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->jktexts[5]));
	labels.push_back(std::make_shared<CLabel>(69, 232, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->jktexts[6]));
	labels.push_back(std::make_shared<CLabel>(213, 232, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->jktexts[7]));

	addUsedEvents(KEYBOARD);
	CHeroWindow::updateArtifacts();
}

void CHeroWindow::keyPressed(EShortcut key)
{
	if(!readOnly && key == EShortcut::ADVENTURE_OPEN_WIKI)
		ENGINE->windows().createAndPushWindow<WikiWindow>(
			WikiWindow::Style::BROWN,
			WikiEntryKey{WikiCategory::HERO, curHero->getHeroType()->getJsonKey()});
}

void CHeroWindow::updateArtifacts()
{
	OBJECT_CONSTRUCTION;

	CWindowWithArtifacts::updateArtifacts();
	auto & heroscrn = LIBRARY->generaltexth->heroscrn;
	assert(curHero);

	name->setText(curHero->getNameTranslated());
	title->setText((boost::format(LIBRARY->generaltexth->allTexts[342]) % curHero->level % curHero->getClassNameTranslated()).str());

	specArea->text = curHero->getHeroType()->getSpecialtyDescriptionTranslated();
	specImage->setFrame(curHero->getHeroType()->imageIndex);
	specName->setText(curHero->getHeroType()->getSpecialtyNameTranslated());

	if(!readOnly)
	{
		tacticsButton = std::make_shared<CToggleButton>(Point(539, 483), AnimationPath::builtin("hsbtns8.def"), std::make_pair(heroscrn[26], heroscrn[31]), 0, EShortcut::HERO_TOGGLE_TACTICS);
		tacticsButton->addHoverText(EButtonState::HIGHLIGHTED, LIBRARY->generaltexth->heroscrn[25]);
		tacticsButton->setSelectedSilent(curHero->tacticFormationEnabled);
	}

	if(dismissButton)
		dismissButton->addHoverText(EButtonState::NORMAL, boost::str(boost::format(LIBRARY->generaltexth->heroscrn[16]) % curHero->getNameTranslated() % curHero->getClassNameTranslated()));
	portraitArea->hoverText = boost::str(boost::format(LIBRARY->generaltexth->allTexts[15]) % curHero->getNameTranslated() % curHero->getClassNameTranslated());
	portraitArea->text = curHero->getBiographyTranslated();
	portraitImage->setFrame(curHero->getIconIndex());

	{
		if(!readOnly && !garr)
		{
			bool removableTroops = curHero->getOwner() == GAME->interface()->playerID;
			std::string helpBox = heroscrn[32];
			boost::algorithm::replace_first(helpBox, "%s", LIBRARY->generaltexth->allTexts[43]);

			garr = std::make_shared<CGarrisonInt>(Point(15, 485), 8, Point(), curHero, nullptr, removableTroops);
			auto split = std::make_shared<CButton>(Point(539, 519), AnimationPath::builtin("hsbtns9.def"), CButton::tooltip(LIBRARY->generaltexth->allTexts[256], helpBox), [this](){ garr->splitClick(); }, EShortcut::HERO_ARMY_SPLIT);
			garr->addSplitBtn(split);
		}
		if(!arts)
		{
			arts = std::make_shared<CArtifactsOfHeroMain>(Point(-65, -8));
			if(!readOnly)
			{
				arts->clickPressedCallback = [this](const CArtPlace & artPlace, const Point & cursorPosition){clickPressedOnArtPlace(curHero, artPlace.slot, true, false, false, cursorPosition);};
				arts->showPopupCallback = [this](CArtPlace & artPlace, const Point & cursorPosition){showArtifactPopup(*arts, artPlace, cursorPosition);};
				arts->gestureCallback = [this](const CArtPlace & artPlace, const Point & cursorPosition){showQuickBackpackWindow(curHero, artPlace.slot, cursorPosition);};
			}
			arts->setHero(curHero);
			addSet(arts);
			if(!readOnly)
				enableKeyboardShortcuts();
		}

		listSelection.reset();
		if(!readOnly)
		{
			int serial = GAME->interface()->cb->getHeroSerial(curHero, false);
			if(serial >= 0)
				listSelection = std::make_shared<CPicture>(ImagePath::builtin("HPSYYY"), 612, 33 + serial * 54);
		}
	}

	//primary skills support
	for(size_t g=0; g<primSkillAreas.size(); ++g)
	{
		int value = curHero->getPrimSkillLevel(static_cast<PrimarySkill>(g));
		primSkillAreas[g]->component.value = value;
		primSkillValues[g]->setText(std::to_string(value));
	}

	//secondary skills support
	for(size_t g=0; g < secSkills.size(); ++g)
	{
		int offset = secSkillSlider ? secSkillSlider->getValue() * 2 : 0;
		if(curHero->secSkills.size() < g + offset + 1)
		{
			secSkillNames[g]->setText("");
			secSkillValues[g]->setText("");
			secSkills[g]->setSkill(SecondarySkill::NONE);
			break;
		}
		SecondarySkill skill = curHero->secSkills[g + offset].first;
		int	level = curHero->getSecSkillLevel(skill);
		std::string skillName = skill.toEntity(LIBRARY)->getNameTranslated();
		std::string skillValue = LIBRARY->generaltexth->levels[level-1];

		secSkillNames[g]->setText(skillName);
		secSkillValues[g]->setText(skillValue);
		secSkills[g]->setSkill(skill, level);
	}

	std::ostringstream expstr;
	expstr << curHero->exp;
	expValue->setText(expstr.str());

	std::ostringstream manastr;
	manastr << curHero->mana << '/' << curHero->manaLimit();
	manaValue->setText(manastr.str());

	//printing experience - original format does not support ui64
	expArea->text = LIBRARY->generaltexth->allTexts[2];
	boost::replace_first(expArea->text, "%d", std::to_string(curHero->level));
	boost::replace_first(expArea->text, "%d", std::to_string(LIBRARY->heroh->reqExp(curHero->level+1)));
	boost::replace_first(expArea->text, "%d", std::to_string(curHero->exp));

	//printing spell points, boost::format can't be used due to locale issues
	spellPointsArea->text = LIBRARY->generaltexth->allTexts[205];
	boost::replace_first(spellPointsArea->text, "%s", curHero->getNameTranslated());
	boost::replace_first(spellPointsArea->text, "%d", std::to_string(curHero->mana));
	boost::replace_first(spellPointsArea->text, "%d", std::to_string(curHero->manaLimit()));

	//if we have exchange window with this curHero open
	bool noDismiss=false;

	if(!readOnly)
		for(auto cew : ENGINE->windows().findWindows<CExchangeWindow>())
		{
			if (cew->holdsGarrison(curHero))
				noDismiss = true;
		}

	//if player only have one hero and no towns
	if(!readOnly && !GAME->interface()->cb->howManyTowns() && GAME->interface()->cb->howManyHeroes() == 1)
		noDismiss = true;

	if(curHero->isMissionCritical())
		noDismiss = true;

	if(!readOnly && GAME->interface()->cb->getMapHeader()->battleOnly)
	{
		const auto * playerState = GAME->interface()->cb->getPlayerState(curHero->tempOwner, false);
		if(playerState && playerState->arenaMainHero == curHero->id)
			noDismiss = true;
	}

	if(dismissButton)
		dismissButton->block(noDismiss);

	if(!readOnly && curHero->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION) == 0)
	{
		tacticsButton->block(true);
	}
	else if(!readOnly)
	{
		tacticsButton->block(false);
		tacticsButton->addCallback([this](bool on){ GAME->interface()->cb->setTactics(curHero, on); });
	}

	if(formations)
	{
		formations->resetCallback();
		//setting formations
		formations->setSelected(curHero->formation == EArmyFormation::TIGHT ? 1 : 0);
		formations->addCallback([this](int value){ GAME->interface()->cb->setFormation(curHero, static_cast<EArmyFormation>(value));});
	}

	morale->set(curHero);
	luck->set(curHero);

	redraw();
}

void CHeroWindow::dismissCurrent()
{
	GAME->interface()->showYesNoDialog(LIBRARY->generaltexth->allTexts[22], [this]()
		{
			arts->putBackPickedArtifact();
			close();
			GAME->interface()->cb->dismissHero(curHero);
			arts->setHero(nullptr);
		}, nullptr);
}

void CHeroWindow::createBackpackWindow()
{
	ENGINE->windows().createAndPushWindow<CHeroBackpackWindow>(curHero, artSets);
}

void CHeroWindow::commanderWindow()
{
	const auto pickedArtInst = getPickedArtifact();
	const auto hero = getHeroPickedArtifact();

	if(pickedArtInst)
	{
		const auto freeSlot = ArtifactUtils::getArtAnyPosition(curHero->getCommander(), pickedArtInst->getTypeId());
		if(vstd::contains(ArtifactUtils::commanderSlots(), freeSlot)) // We don't want to put it in commander's backpack!
		{
			ArtifactLocation dst(curHero->id, freeSlot);
			dst.creature = SlotID::COMMANDER_SLOT_PLACEHOLDER;
			GAME->interface()->cb->swapArtifacts(ArtifactLocation(hero->id, ArtifactPosition::TRANSITION_POS), dst);
		}
	}
	else
	{
		ENGINE->windows().createAndPushWindow<CStackWindow>(curHero->getCommander(), false);
	}
}

void CHeroWindow::updateGarrisons()
{
	if(garr)
		garr->recreateSlots();
	morale->set(curHero);
}

void CHeroWindow::refreshArenaMainHeroMarkers()
{
	for(const auto & switcher : heroList)
		switcher->refreshArenaMainHeroMarker();
	if(questlogButton)
	{
		const auto * playerState = GAME->interface()->cb->getPlayerState(curHero->tempOwner, false);
		questlogButton->block(playerState && playerState->arenaMainHero == curHero->id);
	}
	redraw();
}

bool CHeroWindow::holdsGarrison(const CArmedInstance * army)
{
	return army == curHero;
}
