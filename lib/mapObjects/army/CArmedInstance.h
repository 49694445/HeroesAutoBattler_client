/*
 * CArmedInstance.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CCreatureSet.h"

#include "../CGObjectInstance.h"

#include "../../bonuses/BonusCache.h"
#include "../../bonuses/CBonusSystemNode.h"

#include <vcmi/Entity.h>

VCMI_LIB_NAMESPACE_BEGIN

class BattleInfo;
class CGameState;
class JsonSerializeFormat;

class DLL_LINKAGE CArmedInstance: public CGObjectInstance, public CBonusSystemNode, public CCreatureSet, public IConstBonusProvider
{
private:
	BonusValueCache nonEvilAlignmentMix;

	void attachUnitsToArmy();

protected:
	virtual CBonusSystemNode & whereShouldBeAttached(CGameState & gs);
	virtual CBonusSystemNode & whatShouldBeAttached();

public:
	BattleInfo * battle; //set to the current battle, if engaged

	void randomizeArmy(FactionID type);
	virtual void updateMoraleBonusFromArmy();

	void armyChanged() override;
	CArmedInstance * getArmy() final
	{
		return this;
	}
	const CArmedInstance * getArmy() const final
	{
		return this;
	}

	//////////////////////////////////////////////////////////////////////////
	//IConstBonusProvider
	const IBonusBearer * getBonusBearer() const override;

	void attachToBonusSystem(CGameState & gs) override;
	void detachFromBonusSystem(CGameState & gs) override;
	void restoreBonusSystem(CGameState & gs) override;
	//////////////////////////////////////////////////////////////////////////

	CArmedInstance(IGameInfoCallback * cb);
	CArmedInstance(IGameInfoCallback * cb, BonusNodeType nodeType, bool isHypothetic);

	PlayerColor getOwner() const override
	{
		return this->tempOwner;
	}

	TerrainId getCurrentTerrain() const;

	void serializeJsonOptions(JsonSerializeFormat & handler) override;

	template<typename Handler>
	void serialize(Handler & h)
	{
		h & static_cast<CGObjectInstance &>(*this);
		h & static_cast<CBonusSystemNode &>(*this);
		h & static_cast<CCreatureSet &>(*this);

		// Network-created armed objects need the same local bonus-tree links as
		// objects restored from a save. Without this, their stack instances keep
		// the creature-type parent but have no army parent, so battle-wide auras
		// are present on the server and missing on the client.
		if(!h.saving)
			attachUnitsToArmy();
	}
};

VCMI_LIB_NAMESPACE_END
