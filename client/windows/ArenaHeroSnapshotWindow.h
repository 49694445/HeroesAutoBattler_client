/*
 * ArenaHeroSnapshotWindow.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */

#pragma once

#include "CWindowObject.h"
#include "../../lib/networkPacks/PacksForClient.h"

class CAnimImage;
class CButton;
class CComponent;
class CLabel;
class TransparentFilledRectangle;

/// Clickable portrait backed only by a detached previous-battle snapshot.
class ArenaHeroSnapshotPortrait final : public CIntObject
{
	ArenaHeroSnapshot snapshot;
	std::shared_ptr<TransparentFilledRectangle> frame;
	std::shared_ptr<CAnimImage> portrait;
	std::shared_ptr<CLabel> caption;

public:
	ArenaHeroSnapshotPortrait(Point position, const ArenaHeroSnapshot & snapshot, std::string caption = {});
	void setSnapshot(const ArenaHeroSnapshot & snapshot);
	void clickPressed(const Point & cursorPosition) override;
};

/// Read-only hero presentation which never resolves a live hero/artifact/stack.
class ArenaHeroSnapshotWindow final : public CWindowObject
{
	ArenaHeroSnapshot snapshot;
	std::shared_ptr<CButton> closeButton;
	std::vector<std::shared_ptr<CIntObject>> content;

public:
	explicit ArenaHeroSnapshotWindow(const ArenaHeroSnapshot & snapshot);
};

void showArenaHeroSnapshotWindow(const ArenaHeroSnapshot & snapshot);
