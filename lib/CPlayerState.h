/*
 * CPlayerState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <vcmi/Player.h>
#include <vcmi/Team.h>

#include "callback/GameCallbackHolder.h"
#include "ResourceSet.h"
#include "TurnTimerInfo.h"
#include "bonuses/Bonus.h"
#include "bonuses/CBonusSystemNode.h"
#include "mapObjects/CGObjectInstance.h"
#include "mapping/MapTilesStorage.h"

VCMI_LIB_NAMESPACE_BEGIN

class CGObjectInstance;
class CGHeroInstance;
class CGTownInstance;
class CGDwelling;
struct QuestInfo;

class DLL_LINKAGE PlayerState : public CBonusSystemNode, public Player, public GameCallbackHolder
{
	struct VisitedObjectGlobal
	{
		MapObjectID id;
		MapObjectSubID subID;

		bool operator < (const VisitedObjectGlobal & other) const
		{
			if (id != other.id)
				return id < other.id;
			else
				return subID < other.subID;
		}

		template <typename Handler> void serialize(Handler &h)
		{
			h & id;
			subID.serializeIdentifier(h, id);
		}
	};

	std::vector<ObjectInstanceID> ownedObjects;

	template<typename T>
	std::vector<T> getObjectsOfType() const;

public:
	PlayerColor color;
	bool human = false; //true if human controlled player, false for AI
	TeamID team;
	TResources resources;

	/// list of objects that were "destroyed" by player, either via simple pick-up (e.g. resources) or defeated heroes or wandering monsters
	std::set<ObjectInstanceID> destroyedObjects;
	std::set<ObjectInstanceID> visitedObjects; // as a std::set, since most accesses here will be from visited status checks
	std::set<VisitedObjectGlobal> visitedObjectsGlobal;
	std::vector<QuestInfo> quests; //store info about all received quests
	std::vector<Bonus> battleBonuses; //additional bonuses to be added during battle with neutrals
	std::map<uint32_t, std::map<ArtifactPosition, ArtifactID>> costumesArtifacts;
	std::unique_ptr<JsonNode> playerLocalSettings; // Json with client-defined data, such as order of heroes or current hero paths. Not used by client/lib

	bool cheated;
	bool enteredWinningCheatCode, enteredLosingCheatCode; //if true, this player has entered cheat codes for loss / victory
	EPlayerStatus status;
	std::optional<ui8> daysWithoutCastle;
	TurnTimerInfo turnTimer;

	/// ARENA: player health. The only value that accumulates across rounds and decides the match.
	/// Reaching 0 ends the game. Initial value comes from server-side arena configuration.
	/// Only the value lives here (the client must render it); all rules that change it stay in server/.
	int32_t arenaHealth = 0;

	/// ARENA: the server-authoritative hero that owns the player's army and artifacts.
	/// Initialized from the map's mainHeroInstance. The object ID is state, not a rule:
	/// server code decides which owned hero may be selected, while clients only use the
	/// published ID to render town visits and arena army UI.
	ObjectInstanceID arenaMainHero;

	/// ARENA: recruitment experience, keyed by creature type.
	/// The same creature may occupy several army slots, but they all share one entry.
	/// Only the value lives here (the client must render it in the recruit UI);
	/// the formulas that grant and spend it stay in server/.
	std::map<CreatureID, int64_t> arenaCreatureExperience;

	/// ARENA: experience needed before this creature may be recruited, keyed by creature type.
	/// Computed by the server from the arena recruitment formula and pushed here purely so the
	/// client can draw a progress bar; the formula itself stays in server/. An absent entry means
	/// the server has not published a threshold for that creature yet.
	std::map<CreatureID, int64_t> arenaRecruitThreshold;

	/// ARENA: locked recruitment level, keyed by creature type. A base dwelling submitted at the
	/// default level 1 leaves this absent until the first upgraded dwelling, which is the final
	/// choice. It scales both the experience threshold and how many units one recruitment yields.
	/// The upper bound and the cost of raising it are server-side configuration.
	std::map<CreatureID, int32_t> arenaRecruitLevel;

	/// ARENA: how many times this creature has already been recruited (the `n` in the 1.1^n term).
	/// Absent entry means zero.
	std::map<CreatureID, int32_t> arenaRecruitCount;

	/// ARENA: what one recruitment of this creature costs, as published by the server.
	/// Held so the recruit button can grey itself out as the purse changes without asking
	/// the server again. Absent entry means no recruitment has been offered yet.
	std::map<CreatureID, TResources> arenaRecruitCost;

	/// ARENA: how many units one recruitment of this creature yields. Absent entry means zero.
	std::map<CreatureID, int32_t> arenaRecruitAmount;

	/// ARENA: creature types some town of this player has unlocked, and which may therefore
	/// be recruited or summoned. Membership is decided by the server.
	std::set<CreatureID> arenaUnlockedCreatures;

	/// ARENA: summons left this round. Refreshed at the start of every deployment phase.
	/// Summoning brings a creature type into the army for the first time; recruiting only grows a
	/// type that is already there. The refresh amount is server-side configuration.
	int32_t arenaSummonsRemaining = 0;

	PlayerState(IGameInfoCallback *cb);
	~PlayerState();

	std::string nodeName() const override;

	PlayerColor getId() const override;
	TeamID getTeam() const override;
	bool isHuman() const override;
	const IBonusBearer * getBonusBearer() const override;
	int getResourceAmount(int type) const override;

	int32_t getIndex() const override;
	int32_t getIconIndex() const override;
	std::string getJsonKey() const override;
	std::string getModScope() const override;
	std::string getNameTranslated() const override;
	std::string getNameTextID() const override;
	void registerIcons(const IconRegistar & cb) const override;

	std::vector<const CGHeroInstance* > getHeroes() const;
	std::vector<const CGTownInstance* > getTowns() const;
	std::vector<CGHeroInstance* > getHeroes();
	std::vector<CGTownInstance* > getTowns();

	std::vector<const CGObjectInstance* > getOwnedObjects() const;

	void addOwnedObject(CGObjectInstance * object);
	void removeOwnedObject(CGObjectInstance * object);

	bool checkVanquished() const
	{
		return getHeroes().empty() && getTowns().empty();
	}

	template <typename Handler> void serialize(Handler &h)
	{
		h & color;
		h & human;
		h & team;
		h & resources;
		h & status;
		h & turnTimer;
		h & *playerLocalSettings;
		if (h.hasFeature(Handler::Version::NO_RAW_POINTERS_IN_SERIALIZER))
			h & ownedObjects;
		else
		{
			std::vector<std::shared_ptr<CGObjectInstance>> objectPtrs;
			h & objectPtrs;
			for (const auto & ptr : objectPtrs)
				ownedObjects.push_back(ptr->id);
		}

		h & quests;
		h & visitedObjects;
		h & visitedObjectsGlobal;
		h & status;
		h & daysWithoutCastle;
		h & cheated;
		h & battleBonuses;
		h & costumesArtifacts;
		h & enteredLosingCheatCode;
		h & enteredWinningCheatCode;
		h & static_cast<CBonusSystemNode&>(*this);
		h & destroyedObjects;
		h & arenaHealth;
		h & arenaMainHero;
		h & arenaCreatureExperience;
		h & arenaRecruitThreshold;
		h & arenaRecruitLevel;
		h & arenaRecruitCount;
		h & arenaRecruitCost;
		h & arenaRecruitAmount;
		h & arenaUnlockedCreatures;
		h & arenaSummonsRemaining;
	}
};

struct DLL_LINKAGE TeamState : public CBonusSystemNode
{
public:
	TeamID id; //position in gameState::teams
	std::set<PlayerColor> players; // members of this team
	//TODO: bool if possible
	MapTilesStorage<uint8_t> fogOfWarMap; //true - visible, false - hidden

	std::set<ObjectInstanceID> scoutedObjects;

	TeamState();

	template <typename Handler> void serialize(Handler &h)
	{
		h & id;
		h & players;
		h & fogOfWarMap;
		h & static_cast<CBonusSystemNode&>(*this);
		h & scoutedObjects;
	}

};

VCMI_LIB_NAMESPACE_END
