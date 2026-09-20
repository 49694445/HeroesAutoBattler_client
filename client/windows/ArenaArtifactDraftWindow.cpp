/*
 * ArenaArtifactDraftWindow.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "ArenaArtifactDraftWindow.h"
#include "ArenaHeroSnapshotWindow.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../media/ISoundPlayer.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

#include "../../lib/callback/CCallback.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSoundBase.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
std::weak_ptr<ArenaArtifactDraftWindow> arenaArtifactDraftWindow;

std::string tr(const std::string & textID)
{
	return LIBRARY->generaltexth->translate(textID);
}

int32_t selectedCount(uint32_t value)
{
	int32_t result = 0;
	while(value)
	{
		result += static_cast<int32_t>(value & 1U);
		value >>= 1U;
	}
	return result;
}

std::string selectionText(int32_t selected, int32_t pickCount, int32_t giveUpGoldPerSlot)
{
	const int32_t remaining = std::max(0, pickCount - selected);
	return boost::str(boost::format(tr("vcmi.arena.artifactDraft.selection"))
		% selected % pickCount % remaining % (static_cast<int64_t>(remaining) * giveUpGoldPerSlot));
}

std::string resourceCostText(const TResources & cost)
{
	static const std::array<const char *, 7> nameIDs = {
		"vcmi.arena.artifactDraft.resource.wood",
		"vcmi.arena.artifactDraft.resource.mercury",
		"vcmi.arena.artifactDraft.resource.ore",
		"vcmi.arena.artifactDraft.resource.sulfur",
		"vcmi.arena.artifactDraft.resource.crystal",
		"vcmi.arena.artifactDraft.resource.gems",
		"vcmi.arena.artifactDraft.resource.gold"};
	std::string result;
	for(size_t resource = 0; resource < nameIDs.size(); ++resource)
	{
		if(cost[resource] <= 0)
			continue;
		if(!result.empty())
			result += " · ";
		result += std::to_string(cost[resource]) + tr(nameIDs[resource]);
	}
	return result;
}
}

ArenaArtifactDraftWindow::ArenaArtifactDraftWindow(const ArenaArtifactDraftSelection & pack)
	: CWindowObject(BORDERED)
	, queryID(pack.queryID)
	, pickCount(pack.pickCount)
	, giveUpGoldPerSlot(pack.giveUpGoldPerSlot)
	, remainingMilliseconds(std::max(0, pack.timeoutSeconds) * 1000)
	, remainingResources(GAME->interface()->cb->getResourceAmount())
	, candidates(pack.candidates)
{
	if(const auto * playerState = GAME->interface()->cb->getPlayerState(GAME->interface()->playerID))
		remainingSummons = std::max(0, playerState->arenaSummonsRemaining);

	OBJECT_CONSTRUCTION;
	addUsedEvents(TIME);

	// This is deliberately a standalone dialog: a draft contains candidate cards,
	// not a hero's equipment slots.
	// 590 * 1.3 = 767.  Keep the native-sized cards and center their grid so
	// the added width becomes breathing room around the two snapshot portraits.
	pos.w = 767;
	pos.h = 430;
	updateShadow();
	center();
	backgroundTexture = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), Rect(0, 0, pos.w, pos.h));

	auto title = std::make_shared<CLabel>(pos.w / 2, 20, EFonts::FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, tr("vcmi.arena.artifactDraft.title"));
	auto subtitle = std::make_shared<CLabel>(pos.w / 2, 43, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, tr("vcmi.arena.artifactDraft.subtitle"));
	selectionLabel = std::make_shared<CLabel>(pos.w / 2, 65, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE);
	if(const auto & previousOwnHero = GAME->interface()->getArenaPreviousOwnHero())
		previousOwnHeroPortrait = std::make_shared<ArenaHeroSnapshotPortrait>(Point(10, 3), *previousOwnHero, LIBRARY->generaltexth->translate("vcmi.arena.snapshot.ownSide"));
	if(const auto & previousEnemyHero = GAME->interface()->getArenaPreviousEnemyHero())
		previousEnemyHeroPortrait = std::make_shared<ArenaHeroSnapshotPortrait>(Point(pos.w - 74, 3), *previousEnemyHero, LIBRARY->generaltexth->translate("vcmi.arena.snapshot.enemySide"));
	candidateSelections.resize(candidates.size());
	unavailableLabels.resize(candidates.size());
	candidateFrames.reserve(candidates.size());

	constexpr int columns = 4;
	constexpr int cardWidth = 132;
	constexpr int cardHeight = 138;
	constexpr int cardGap = 6;
	constexpr int gridWidth = columns * cardWidth + (columns - 1) * cardGap;
	const int gridLeft = (pos.w - gridWidth) / 2;
	constexpr int gridTop = 86;
	constexpr int rowGap = 7;

	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const int column = static_cast<int>(index % columns);
		const int row = static_cast<int>(index / columns);
		const int cardX = gridLeft + column * (cardWidth + cardGap);
		const int cardY = gridTop + row * (cardHeight + rowGap);
		const int y = cardY + 8;
		const auto & candidate = candidates[index];

		candidateFrames.push_back(std::make_shared<TransparentFilledRectangle>(
			Rect(cardX, cardY, cardWidth, cardHeight),
			ColorRGBA(0, 0, 0, 70),
			ColorRGBA(128, 100, 75)));

		if(candidate.isArtifact)
		{
			// CSelectableComponent provides the native bright-yellow H3 selection
			// border. The callback is attached to the icon itself: no extra choice
			// button is created for any candidate.
			auto selectable = std::make_shared<CSelectableComponent>(ComponentType::ARTIFACT, candidate.artifact, 0, CComponent::large, [this, index](){ choose(index); });
			candidateSelections[index] = selectable;
			candidateIcons.push_back(selectable);
		}
		else
		{
			if(candidate.available)
			{
				auto selectable = std::make_shared<CSelectableComponent>(ComponentType::CREATURE, candidate.creature, candidate.amount, CComponent::large, [this, index](){ choose(index); });
				candidateSelections[index] = selectable;
				candidateIcons.push_back(selectable);
			}
			else
				candidateIcons.push_back(std::make_shared<CComponent>(ComponentType::CREATURE, candidate.creature, candidate.amount, CComponent::large));
			creatureLevelLabels.push_back(std::make_shared<CLabel>(
				cardX + cardWidth / 2, y + 55, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW,
				"Lv" + std::to_string(candidate.recruitLevel)));
			creatureCostLabels.push_back(std::make_shared<CLabel>(
				cardX + cardWidth / 2, cardY + 111, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW,
				resourceCostText(candidate.cost)));
			unavailableLabels[index] = std::make_shared<CLabel>(
				cardX + cardWidth / 2, cardY + 127, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::ORANGE,
				candidate.available ? "" : tr("vcmi.arena.artifactDraft.unavailable"));
		}
		// Move the complete component tree (icon plus subtitle). Updating only
		// CComponent::pos leaves its child images at the window origin.
		// Artifact and creature images have different widths, so center from the
		// actual image instead of assuming every large component is 82 pixels.
		const int x = cardX + (cardWidth - candidateIcons.back()->image->pos.w) / 2;
		candidateIcons.back()->moveBy(Point(x, y));
		if(candidate.isArtifact)
		{
			// y is window-local. candidateIcons.back()->pos.y is already absolute,
			// so using it here would apply the window offset twice to the label.
			const int costY = y + candidateIcons.back()->pos.h + 17;
			artifactCostLabels.push_back(std::make_shared<CLabel>(
				cardX + cardWidth / 2,
				costY,
				EFonts::FONT_SMALL,
				ETextAlignment::CENTER,
				Colors::YELLOW,
				std::to_string(candidate.goldCost)));
		}
	}

	giveUpButton = std::make_shared<CButton>(Point(pos.w / 2 - 40, 374), AnimationPath::builtin("settingsWindow/button80"), CButton::tooltip(), [this](){ submit(); }, EShortcut::GLOBAL_ACCEPT);
	giveUpButton->setTextOverlay(tr("vcmi.arena.artifactDraft.giveUp"), EFonts::FONT_SMALL, Colors::WHITE);
	countdownLabel = std::make_shared<CLabel>(pos.w / 2, 414, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW);
	refreshSelectionLabel();
	refreshCountdownLabel();
}

void ArenaArtifactDraftWindow::choose(size_t index)
{
	if(submitted || index >= candidates.size() || selectedMask & (uint32_t(1) << index))
		return;
	if(!candidates[index].isArtifact && (!candidates[index].available || remainingSummons <= 0
		|| !remainingResources.canAfford(candidates[index].cost)))
		return;
	if(selectedCount(selectedMask) >= pickCount)
		return;

	ENGINE->sound().playSound(soundBase::button);
	selectedMask |= uint32_t(1) << index;
	if(candidates[index].isArtifact)
		remainingResources[EGameResID::GOLD] -= candidates[index].goldCost;
	else
	{
		remainingResources -= candidates[index].cost;
		--remainingSummons;
	}
	if(candidateSelections[index])
		candidateSelections[index]->select(true);
	if(auto * interface = GAME->interface())
		interface->cb->arenaArtifactDraftPick(queryID, static_cast<int32_t>(index));
	refreshSelectionLabel();
	refreshCandidateAvailability();
	if(selectedCount(selectedMask) == pickCount)
	{
		submitted = true;
		close();
	}
}

void ArenaArtifactDraftWindow::refreshCandidateAvailability()
{
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		if(candidates[index].isArtifact || !unavailableLabels[index] || (selectedMask & (uint32_t(1) << index)))
			continue;
		const bool available = candidates[index].available && remainingSummons > 0
			&& remainingResources.canAfford(candidates[index].cost);
		unavailableLabels[index]->setText(available ? "" : tr("vcmi.arena.artifactDraft.unavailable"));
	}
}

void ArenaArtifactDraftWindow::submit()
{
	if(submitted)
		return;
	submitted = true;
	if(auto * interface = GAME->interface())
		interface->cb->sendQueryReply(0, queryID);
	close();
}

void ArenaArtifactDraftWindow::refreshSelectionLabel()
{
	selectionLabel->setText(selectionText(selectedCount(selectedMask), pickCount, giveUpGoldPerSlot));
}

void ArenaArtifactDraftWindow::refreshCountdownLabel()
{
	const int32_t seconds = (remainingMilliseconds + 999) / 1000;
	if(seconds == displayedSeconds)
		return;
	displayedSeconds = seconds;
	countdownLabel->setText(boost::str(boost::format(tr("vcmi.arena.artifactDraft.countdown")) % seconds));
}

void ArenaArtifactDraftWindow::tick(uint32_t msPassed)
{
	if(submitted || remainingMilliseconds <= 0)
		return;
	remainingMilliseconds = std::max<int32_t>(0, remainingMilliseconds - static_cast<int32_t>(std::min<uint32_t>(msPassed, std::numeric_limits<int32_t>::max())));
	refreshCountdownLabel();
}

void showArenaArtifactDraftWindow(const ArenaArtifactDraftSelection & pack)
{
	clearArenaArtifactDraftWindow();
	auto window = std::make_shared<ArenaArtifactDraftWindow>(pack);
	arenaArtifactDraftWindow = window;
	ENGINE->windows().pushWindow(window);
}

void clearArenaArtifactDraftWindow()
{
	if(auto window = arenaArtifactDraftWindow.lock())
	{
		// The query reply path may have already popped the dialog before the
		// server's authoritative close packet reaches the main thread. WindowBase
		// rejects closing anything except the current top window, so make this
		// cleanup idempotent instead of closing a disposed window twice.
		if(ENGINE->windows().isTopWindow(window))
			window->close();
	}
	arenaArtifactDraftWindow.reset();
}
