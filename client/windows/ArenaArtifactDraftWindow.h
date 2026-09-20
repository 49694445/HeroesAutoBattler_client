/*
 * ArenaArtifactDraftWindow.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include "CWindowObject.h"

#include "../../lib/networkPacks/PacksForClient.h"

class CButton;
class CComponent;
class CSelectableComponent;
class CFilledTexture;
class CLabel;
class TransparentFilledRectangle;
class ArenaHeroSnapshotPortrait;

/// Client-only presentation of a server-owned arena draft query. Candidate
/// choices are irrevocable locally and committed to the server one at a time;
/// the final reply gives up any slots that remain unfilled.
class ArenaArtifactDraftWindow final : public CWindowObject
{
	QueryID queryID;
	int32_t pickCount;
	int32_t giveUpGoldPerSlot;
	int32_t remainingMilliseconds;
	int32_t displayedSeconds = -1;
	int32_t remainingSummons = 0;
	uint32_t selectedMask = 0;
	bool submitted = false;
	TResources remainingResources;
	std::vector<ArenaArtifactDraftCandidateInfo> candidates;
	std::vector<std::shared_ptr<CComponent>> candidateIcons;
	std::vector<std::shared_ptr<CSelectableComponent>> candidateSelections;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> candidateFrames;
	std::vector<std::shared_ptr<CLabel>> unavailableLabels;
	std::vector<std::shared_ptr<CLabel>> artifactCostLabels;
	std::vector<std::shared_ptr<CLabel>> creatureLevelLabels;
	std::vector<std::shared_ptr<CLabel>> creatureCostLabels;
	std::shared_ptr<CFilledTexture> backgroundTexture;
	std::shared_ptr<CButton> giveUpButton;
	std::shared_ptr<CLabel> selectionLabel;
	std::shared_ptr<CLabel> countdownLabel;
	std::shared_ptr<ArenaHeroSnapshotPortrait> previousOwnHeroPortrait;
	std::shared_ptr<ArenaHeroSnapshotPortrait> previousEnemyHeroPortrait;

	void choose(size_t index);
	void submit();
	void refreshSelectionLabel();
	void refreshCandidateAvailability();
	void refreshCountdownLabel();

public:
	explicit ArenaArtifactDraftWindow(const ArenaArtifactDraftSelection & pack);
	void tick(uint32_t msPassed) override;
};

void showArenaArtifactDraftWindow(const ArenaArtifactDraftSelection & pack);
void clearArenaArtifactDraftWindow();
