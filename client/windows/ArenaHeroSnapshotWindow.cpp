/*
 * ArenaHeroSnapshotWindow.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */

#include "StdInc.h"
#include "ArenaHeroSnapshotWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/CComponentHolder.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
/// Pure snapshot-backed counterpart of the native hero-window artifact panel.
/// It deliberately owns no CArtifactInstance and never consults a live hero.
class SnapshotArtifactsPanel final : public CIntObject
{
	std::vector<ArenaHeroSnapshot::Artifact> backpack;
	std::vector<std::shared_ptr<CArtPlace>> backpackPlaces;
	std::vector<std::shared_ptr<CArtPlace>> wornPlaces;
	std::shared_ptr<CButton> leftButton;
	std::shared_ptr<CButton> rightButton;
	size_t firstBackpackArtifact = 0;

	void refreshBackpack()
	{
		for(size_t index = 0; index < backpackPlaces.size(); ++index)
		{
			const size_t artifactIndex = firstBackpackArtifact + index;
			if(artifactIndex >= backpack.size())
			{
				backpackPlaces[index]->setArtifact(ArtifactID(ArtifactID::NONE));
				continue;
			}

			const auto & artifact = backpack[artifactIndex];
			if(artifact.artifact == ArtifactID::SPELL_SCROLL && artifact.scrollSpell == SpellID::NONE)
				backpackPlaces[index]->setArtifact(ArtifactID(ArtifactID::NONE));
			else
				backpackPlaces[index]->setArtifact(artifact.artifact, artifact.scrollSpell);
		}

		leftButton->block(firstBackpackArtifact == 0);
		rightButton->block(firstBackpackArtifact + backpackPlaces.size() >= backpack.size());
		redraw();
	}

	void scrollBackpack(bool left)
	{
		if(left)
		{
			if(firstBackpackArtifact > 0)
				--firstBackpackArtifact;
		}
		else if(firstBackpackArtifact + backpackPlaces.size() < backpack.size())
			++firstBackpackArtifact;
		refreshBackpack();
	}

public:
	explicit SnapshotArtifactsPanel(const std::vector<ArenaHeroSnapshot::Artifact> & artifacts)
	{
		OBJECT_CONSTRUCTION;

		static const std::array<Point, ArtifactPosition::BACKPACK_START> slotPositions = {
			Point(444, 22), Point(503, 234), Point(444, 72), Point(318, 61), Point(497, 176),
			Point(444, 123), Point(366, 61), Point(545, 176), Point(450, 287), Point(318, 135),
			Point(334, 185), Point(350, 236), Point(366, 287), Point(499, 22), Point(545, 22),
			Point(545, 68), Point(545, 114), Point(545, 302), Point(316, 287)};

		for(const auto & artifact : artifacts)
		{
			if(artifact.worn && artifact.position >= ArtifactPosition(0)
				&& artifact.position < ArtifactPosition::BACKPACK_START)
			{
				if(artifact.artifact == ArtifactID::SPELL_SCROLL && artifact.scrollSpell == SpellID::NONE)
					continue;
				auto place = std::make_shared<CArtPlace>(slotPositions.at(artifact.position.num), artifact.artifact, artifact.scrollSpell);
				place->slot = artifact.position;
				wornPlaces.push_back(std::move(place));
			}
			else
				backpack.push_back(artifact);
		}

		std::sort(backpack.begin(), backpack.end(), [](const auto & lhs, const auto & rhs)
		{
			return lhs.position.num < rhs.position.num;
		});

		for(size_t index = 0; index < 5; ++index)
			backpackPlaces.push_back(std::make_shared<CArtPlace>(Point(338 + static_cast<int32_t>(index) * 46, 357)));

		leftButton = std::make_shared<CButton>(Point(314, 356), AnimationPath::builtin("hsbtns3.def"), CButton::tooltip(), [this](){ scrollBackpack(true); }, EShortcut::MOVE_LEFT);
		rightButton = std::make_shared<CButton>(Point(567, 356), AnimationPath::builtin("hsbtns5.def"), CButton::tooltip(), [this](){ scrollBackpack(false); }, EShortcut::MOVE_RIGHT);
		refreshBackpack();
	}
};
}

ArenaHeroSnapshotPortrait::ArenaHeroSnapshotPortrait(Point position, const ArenaHeroSnapshot & snapshot_, std::string caption_)
	: CIntObject(LCLICK, position)
	, snapshot(snapshot_)
{
	OBJECT_CONSTRUCTION;
	pos.w = 64;
	pos.h = caption_.empty() ? 70 : 82;
	frame = std::make_shared<TransparentFilledRectangle>(
		Rect(0, 0, pos.w, pos.h), ColorRGBA(0, 0, 0, 145), ColorRGBA(214, 180, 90));
	const auto * portraitType = snapshot.portraitSource.toHeroType();
	const auto * fallbackType = snapshot.heroType.toHeroType();
	const int32_t icon = portraitType ? portraitType->imageIndex : (fallbackType ? fallbackType->imageIndex : 0);
	portrait = std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsLarge"), icon, 0, 3, 3);
	if(!caption_.empty())
		caption = std::make_shared<CLabel>(pos.w / 2, 70, FONT_TINY, ETextAlignment::CENTER, Colors::YELLOW, std::move(caption_));
}

void ArenaHeroSnapshotPortrait::setSnapshot(const ArenaHeroSnapshot & updatedSnapshot)
{
	snapshot = updatedSnapshot;
	const auto * portraitType = snapshot.portraitSource.toHeroType();
	const auto * fallbackType = snapshot.heroType.toHeroType();
	portrait->setFrame(portraitType ? portraitType->imageIndex : (fallbackType ? fallbackType->imageIndex : 0));
	redraw();
}

void ArenaHeroSnapshotPortrait::clickPressed(const Point & cursorPosition)
{
	showArenaHeroSnapshotWindow(snapshot);
}

ArenaHeroSnapshotWindow::ArenaHeroSnapshotWindow(const ArenaHeroSnapshot & snapshot_)
	: CWindowObject(PLAYER_COLORED, ImagePath::builtin("HeroScr4"))
	, snapshot(snapshot_)
{
	OBJECT_CONSTRUCTION;
	background->setPlayerColor(snapshot.owner);

	auto keep = [this]<typename T>(std::shared_ptr<T> widget)
	{
		content.push_back(widget);
		return widget;
	};

	const auto * heroType = snapshot.heroType.toHeroType();
	const auto * portraitType = snapshot.portraitSource.toHeroType();
	const int32_t portraitIndex = portraitType ? portraitType->imageIndex : (heroType ? heroType->imageIndex : 0);
	const std::string className = heroType && heroType->heroClass ? heroType->heroClass->getNameTranslated() : std::string();
	const std::string title = className.empty()
		? ("Lv" + std::to_string(snapshot.level) + LIBRARY->generaltexth->translate("vcmi.arena.snapshot.titleSuffix"))
		: ("Lv" + std::to_string(snapshot.level) + " " + className + LIBRARY->generaltexth->translate("vcmi.arena.snapshot.titleSuffix"));

	keep(std::make_shared<CAnimImage>(AnimationPath::builtin("CREST58"), snapshot.owner.getNum(), 0, 606, 8));
	if(heroType)
	{
		auto portraitArea = std::make_shared<LRClickableAreaWText>(Rect(18, 18, 58, 64));
		portraitArea->hoverText = snapshot.name + " · " + className;
		portraitArea->text = heroType->getBiographyTranslated();
		keep(portraitArea);
	}
	keep(std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsLarge"), portraitIndex, 0, 19, 19));
	keep(std::make_shared<CLabel>(190, 38, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, snapshot.name));
	keep(std::make_shared<CLabel>(190, 65, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, title));

	for(int32_t skill = 0; skill < GameConstants::PRIMARY_SKILLS; ++skill)
	{
		const int32_t centerX = 53 + skill * 70;
		auto skillArea = std::make_shared<LRClickableAreaWTextComp>(Rect(30 + 70 * skill, 109, 42, 64), ComponentType::PRIM_SKILL);
		skillArea->text = LIBRARY->generaltexth->arraytxt[2 + skill];
		skillArea->hoverText = LIBRARY->generaltexth->primarySkillNames[skill];
		skillArea->component.subType = PrimarySkill(skill);
		skillArea->component.value = snapshot.primarySkills.at(skill);
		keep(skillArea);
		keep(std::make_shared<CLabel>(centerX, 99, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->jktexts[1 + skill]));
		keep(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), skill, 0, 32 + skill * 70, 111));
		keep(std::make_shared<CLabel>(centerX, 166, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, std::to_string(snapshot.primarySkills.at(skill))));
	}

	keep(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 4, 0, 20, 230));
	keep(std::make_shared<CAnimImage>(AnimationPath::builtin("PSKIL42"), 3, 0, 162, 230));
	keep(std::make_shared<LRClickableAreaWText>(Rect(18, 228, 136, 42), LIBRARY->generaltexth->translate("vcmi.arena.snapshot.experience"), LIBRARY->generaltexth->translate("vcmi.arena.snapshot.experiencePrefix") + std::to_string(snapshot.experience)));
	keep(std::make_shared<LRClickableAreaWText>(Rect(162, 228, 136, 42), LIBRARY->generaltexth->translate("vcmi.arena.snapshot.mana"),
		LIBRARY->generaltexth->translate("vcmi.arena.snapshot.manaPrefix") + std::to_string(snapshot.mana) + "/" + std::to_string(snapshot.manaLimit)));
	keep(std::make_shared<CLabel>(69, 232, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->jktexts[6]));
	keep(std::make_shared<CLabel>(213, 232, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->jktexts[7]));
	keep(std::make_shared<CLabel>(68, 252, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, std::to_string(snapshot.experience)));
	keep(std::make_shared<CLabel>(211, 252, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE,
		std::to_string(snapshot.mana) + "/" + std::to_string(snapshot.manaLimit)));

	if(heroType)
	{
		keep(std::make_shared<LRClickableAreaWText>(Rect(18, 180, 136, 42),
			heroType->getSpecialtyNameTranslated(), heroType->getSpecialtyDescriptionTranslated()));
		keep(std::make_shared<CAnimImage>(AnimationPath::builtin("UN44"), heroType->imageIndex, 0, 18, 180));
		keep(std::make_shared<CLabel>(69, 183, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->jktexts[5]));
		keep(std::make_shared<CLabel>(69, 205, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, heroType->getSpecialtyNameTranslated(), 86));
	}
	keep(std::make_shared<CAnimImage>(AnimationPath::builtin("IMRL22"), std::clamp(snapshot.morale + 3, 0, 6), 0, 190, 184));
	keep(std::make_shared<CLabel>(201, 209, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.arena.snapshot.moralePrefix") + std::to_string(snapshot.morale)));
	keep(std::make_shared<CAnimImage>(AnimationPath::builtin("ILCK22"), std::clamp(snapshot.luck + 3, 0, 6), 0, 248, 184));
	keep(std::make_shared<CLabel>(259, 209, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.arena.snapshot.luckPrefix") + std::to_string(snapshot.luck)));

	for(size_t index = 0; index < std::min<size_t>(snapshot.secondarySkills.size(), 8); ++index)
	{
		const auto [skill, level] = snapshot.secondarySkills.at(index);
		const int32_t column = static_cast<int32_t>(index % 2);
		const int32_t row = static_cast<int32_t>(index / 2);
		const Point skillPosition(column == 0 ? 18 : 162, 276 + row * 48);
		keep(std::make_shared<CSecSkillPlace>(skillPosition, CSecSkillPlace::ImageSize::MEDIUM, skill, level));
		const int32_t textX = column == 0 ? 68 : 212;
		const int32_t textWidth = column == 0 ? 87 : 71;
		const int32_t safeLevel = std::clamp<int32_t>(level, 1, 3);
		keep(std::make_shared<CLabel>(textX, 280 + row * 48, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
			LIBRARY->generaltexth->levels[safeLevel - 1], textWidth));
		keep(std::make_shared<CLabel>(textX, 300 + row * 48, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
			skill.toEntity(LIBRARY)->getNameTranslated(), textWidth));
	}

	keep(std::make_shared<SnapshotArtifactsPanel>(snapshot.artifacts));

	for(const auto & stack : snapshot.army)
	{
		if(stack.slot.num < 0 || stack.slot.num >= GameConstants::ARMY_SIZE || stack.creature == CreatureID::NONE)
			continue;
		const int32_t count = static_cast<int32_t>(std::min<int64_t>(stack.count, std::numeric_limits<int32_t>::max()));
		auto component = std::make_shared<CComponent>(ComponentType::CREATURE, stack.creature, count, CComponent::large, FONT_SMALL);
		component->moveBy(Point(15 + stack.slot.num * 66, 485));
		keep(component);
	}

	closeButton = std::make_shared<CButton>(Point(609, 516), AnimationPath::builtin("hsbtns.def"),
		CButton::tooltip(LIBRARY->generaltexth->heroscrn[17]), [this](){ close(); }, EShortcut::GLOBAL_RETURN);
}

void showArenaHeroSnapshotWindow(const ArenaHeroSnapshot & snapshot)
{
	ENGINE->windows().pushWindow(std::make_shared<ArenaHeroSnapshotWindow>(snapshot));
}
