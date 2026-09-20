/*
 * PacksForClient.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "ArtifactLocation.h"
#include "Component.h"
#include "EInfoWindowMode.h"
#include "EOpenWindowMode.h"
#include "EntityChanges.h"
#include "NetPacksBase.h"
#include "ObjProperty.h"

#include "../ResourceSet.h"
#include "../TurnTimerInfo.h"
#include "../bonuses/Bonus.h"
#include "../gameState/EVictoryLossCheckResult.h"
#include "../gameState/RumorState.h"
#include "../gameState/QuestInfo.h"
#include "../gameState/TavernSlot.h"
#include "../gameState/GameStatistics.h"
#include "../int3.h"
#include "../mapObjects/army/CSimpleArmy.h"
#include "../spells/ViewSpellInt.h"

class CClient;
class CGameHandler;

VCMI_LIB_NAMESPACE_BEGIN

class CGameState;
class CArtifact;
class CGObjectInstance;
class CArtifactInstance;
struct StackLocation;
struct ArtSlotInfo;
struct QuestInfo;
class IBattleState;
class BattleInfo;

// This one teleport-specific, but has to be available everywhere in callbacks and netpacks
// For now it's will be there till teleports code refactored and moved into own file
using TTeleportExitsList = std::vector<std::pair<ObjectInstanceID, int3>>;

using FowTilesType = std::set<int3>;

/***********************************************************************************************************/
struct DLL_LINKAGE PackageApplied : public CPackForClient
{
	PackageApplied() = default;
	PackageApplied(PlayerColor player, uint32_t requestID, uint16_t packType, bool result)
		: player(player)
		, requestID(requestID)
		, packType(packType)
		, result(result)
	{
	}

	void visitTyped(ICPackVisitor & visitor) override;

	/// ID of player that sent this package
	PlayerColor player;
	/// request ID, as provided by player
	uint32_t requestID = 0;
	/// type id of applied package
	uint16_t packType = 0;
	/// If false, then pack failed to apply, for example - illegal request
	bool result = false;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & requestID;
		h & packType;
		h & result;
	}
};

struct DLL_LINKAGE PackageReceived : public CPackForClient
{
	PackageReceived() = default;
	PackageReceived(PlayerColor player, uint32_t requestID, uint16_t packType)
		: player(player)
		, requestID(requestID)
		, packType(packType)
	{
	}

	void visitTyped(ICPackVisitor & visitor) override;

	/// ID of player that sent this package
	PlayerColor player;
	/// request ID, as provided by player
	uint32_t requestID;
	/// type id of applied package
	uint16_t packType;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & requestID;
		h & packType;
	}
};

struct DLL_LINKAGE SystemMessage : public CPackForClient
{
	explicit SystemMessage(MetaString Text)
		: text(std::move(Text))
	{
	}
	SystemMessage() = default;

	void visitTyped(ICPackVisitor & visitor) override;

	MetaString text;

	template <typename Handler> void serialize(Handler & h)
	{
		h & text;
	}
};

struct DLL_LINKAGE PlayerBlocked : public CPackForClient
{
	enum EReason { UPCOMING_BATTLE, ONGOING_MOVEMENT };
	enum EMode { BLOCKADE_STARTED, BLOCKADE_ENDED };

	EReason reason = UPCOMING_BATTLE;
	EMode startOrEnd = BLOCKADE_STARTED;
	PlayerColor player;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & reason;
		h & startOrEnd;
		h & player;
	}
};

struct DLL_LINKAGE PlayerCheated : public CPackForClient
{
	PlayerColor player;
	bool localOnlyCheat = false;

	bool losingCheatCode = false;
	bool winningCheatCode = false;
	ColorScheme colorScheme = ColorScheme::KEEP;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & localOnlyCheat;
		h & losingCheatCode;
		h & winningCheatCode;
		h & colorScheme;
	}
};

struct DLL_LINKAGE TurnTimeUpdate : public CPackForClient
{
	PlayerColor player;
	TurnTimerInfo turnTimer;

	void visitTyped(ICPackVisitor & visitor) override;
		
	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & turnTimer;
	}
};

struct DLL_LINKAGE PlayerStartsTurn : public Query
{
	PlayerColor player;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & player;
	}
};

struct DLL_LINKAGE DaysWithoutTown : public CPackForClient
{
	PlayerColor player;
	std::optional<int32_t> daysWithoutCastle;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & daysWithoutCastle;
	}
};

struct DLL_LINKAGE EntitiesChanged : public CPackForClient
{
	std::vector<EntityChanges> changes;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & changes;
	}
};

struct DLL_LINKAGE SetResources : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::ABSOLUTE;
	PlayerColor player;
	ResourceSet res; //res[resid] => res amount

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & player;
		h & res;
	}
};

/// ARENA: changes how much recruitment experience a player has for one creature type.
///
/// Experience is keyed by creature type, not by army slot: the same creature may
/// occupy several slots but they all share one entry. One pack per creature that
/// changed - there is no need to resend the whole table.
///
/// Broadcast to everyone. Experience is settled from a battle both players just
/// watched, so either side could derive it anyway; sending it leaks nothing new.
/// This is NOT the case for deployment actions (recruiting, summoning) - those
/// must be answered only to the player who sent them, see section 4.16b of the
/// technical design document.
///
/// Carries a value only. Every rule that grants or spends experience stays in
/// server/, per section 1.1.
struct DLL_LINKAGE SetArenaCreatureExperience : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::RELATIVE;
	PlayerColor player;
	CreatureID creature;
	int64_t experience = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & player;
		h & creature;
		h & experience;
	}
};

/// ARENA: publishes one creature's recruitment bookkeeping to the client.
///
/// All three values are absolute - the server recomputes the record and sends the
/// whole thing, so there is no relative mode to get out of step.
///
/// `threshold` is the experience needed before this creature may be recruited. The
/// server computes it from the arena recruitment formula and sends the result; the
/// formula stays in server/ per section 1.1, so the client can draw a progress bar
/// without carrying the rules.
///
/// UNLIKE SetArenaCreatureExperience, this pack must NOT be broadcast while a
/// deployment phase is running. Setting a recruitment level, or recruiting, is a
/// deployment action, and deployment is played face-down: send it only to the
/// owning player (see section 4.16b of the technical design document). Broadcasting
/// it would tell the opponent what the player just bought.
struct DLL_LINKAGE SetArenaRecruitState : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	CreatureID creature;
	/// Experience required to recruit. Zero means the server has not published one yet.
	int64_t threshold = 0;
	/// Recruitment level; 1 is the default and means the player never raised it.
	int32_t level = 1;
	/// Whether the family's one-time recruitment-level choice has actually been
	/// committed. A displayed default level alone must not consume that choice.
	bool levelLocked = false;
	/// How many times this creature has already been recruited.
	int32_t recruitCount = 0;
	/// What one recruitment costs, already worked out by the server. The client needs
	/// this to grey out the recruit button the moment the player spends gold elsewhere,
	/// without a round trip - comparing it against the player's purse is not a rule, but
	/// deriving it would be, so the number is sent rather than recomputed.
	TResources cost;
	/// How many units one recruitment yields.
	int32_t amount = 0;
	/// Whether a town the player owns has unlocked this creature. Sent rather than looked
	/// up client-side so that what counts as "unlocked" stays a single server-side answer.
	bool unlocked = false;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & creature;
		h & threshold;
		h & level;
		h & levelLocked;
		h & recruitCount;
		h & cost;
		h & amount;
		h & unlocked;
	}
};

/// ARENA: authoritative time remaining for a hero secondary-skill choice.
/// The client only displays this value; the server makes the choice on expiry.
struct DLL_LINKAGE SetArenaHeroLevelUpChoiceTimeout : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	QueryID queryID = QueryID::NONE;
	/// Negative clears a completed flow from the client cache.
	int32_t remainingSeconds = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & queryID;
		h & remainingSeconds;
	}
};

/// ARENA: authoritative remaining time for one player's battle-result acknowledgement.
struct DLL_LINKAGE SetArenaBattleResultTimeout : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	BattleID battleID = BattleID::NONE;
	QueryID queryID = QueryID::NONE;
	/// Negative clears the finished acknowledgement from the client cache.
	int32_t remainingSeconds = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & battleID;
		h & queryID;
		h & remainingSeconds;
	}
};

/// ARENA: one server-computed recruitment-progress value shown during battle.
///
/// This is deliberately separate from SetArenaRecruitState and CPlayerState: values
/// may include experience provisionally earned in the current battle, so applying
/// them to persistent recruitment state would corrupt save/reconnect semantics.
struct DLL_LINKAGE ArenaBattleCreatureProgressEntry
{
	PlayerColor player = PlayerColor::NEUTRAL;
	CreatureID creature = CreatureID::NONE;
	int32_t level = 1;
	int64_t currentExperience = 0;
	int64_t maximumExperience = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & creature;
		h & level;
		h & currentExperience;
		h & maximumExperience;
	}
};

/// ARENA: complete read-only creature-progress snapshot for the active battle.
///
/// The server recomputes this after every completed combat round from authoritative
/// casualties and survival-round weights. Clients replace their cache for battleID
/// and completedRound; they must not derive recruitment formulas from battle data.
struct DLL_LINKAGE SetArenaBattleCreatureProgress : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	BattleID battleID = BattleID::NONE;
	int32_t completedRound = 0;
	std::vector<ArenaBattleCreatureProgressEntry> entries;

	template <typename Handler> void serialize(Handler & h)
	{
		h & battleID;
		h & completedRound;
		h & entries;
	}
};

/// ARENA: sets how many summons a player has left this round.
///
/// Summoning brings a creature type into the army for the first time; recruiting
/// grows a type that is already present. The allowance is refreshed each round from
/// server-side configuration - this pack carries the resulting number only.
///
/// Same face-down rule as SetArenaRecruitState: during deployment, send only to the
/// owning player, never broadcast.
struct DLL_LINKAGE SetArenaSummonsRemaining : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::ABSOLUTE;
	PlayerColor player;
	int32_t summons = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & player;
		h & summons;
	}
};

/// Detached, server-authoritative view of one hero at an arena battle start.
///
/// This intentionally contains entity type IDs and scalar values only.  In
/// particular it never contains a hero, stack, or artifact instance ID: those
/// objects continue to change during the following deployment and must not be
/// consulted when the previous battle is inspected.
struct DLL_LINKAGE ArenaHeroSnapshot
{
	struct ArmyStack
	{
		SlotID slot;
		CreatureID creature = CreatureID::NONE;
		int64_t count = 0;

		template <typename Handler> void serialize(Handler & h)
		{
			h & slot;
			h & creature;
			h & count;
		}
	};

	struct Artifact
	{
		ArtifactPosition position = ArtifactPosition::PRE_FIRST;
		ArtifactID artifact = ArtifactID::NONE;
		SpellID scrollSpell = SpellID::NONE;
		bool worn = false;

		template <typename Handler> void serialize(Handler & h)
		{
			h & position;
			h & artifact;
			h & scrollSpell;
			h & worn;
		}
	};

	PlayerColor owner = PlayerColor::NEUTRAL;
	HeroTypeID heroType = HeroTypeID::NONE;
	HeroTypeID portraitSource = HeroTypeID::NONE;
	std::string name;
	uint32_t level = 0;
	int64_t experience = 0;
	int32_t mana = 0;
	int32_t manaLimit = 0;
	int32_t morale = 0;
	int32_t luck = 0;
	std::array<int32_t, GameConstants::PRIMARY_SKILLS> primarySkills{};
	std::vector<std::pair<SecondarySkill, uint8_t>> secondarySkills;
	std::vector<ArmyStack> army;
	std::vector<Artifact> artifacts;

	template <typename Handler> void serialize(Handler & h)
	{
		h & owner;
		h & heroType;
		h & portraitSource;
		h & name;
		h & level;
		h & experience;
		h & mana;
		h & manaLimit;
		h & morale;
		h & luck;
		h & primarySkills;
		h & secondarySkills;
		h & army;
		h & artifacts;
	}
};

/// ARENA: tells clients that a deployment phase has begun and how long it lasts.
///
/// Sent privately to the player whose deployment is now unlocked. Both sides
/// are unlocked only after every post-battle dialog has completed.
struct DLL_LINKAGE ArenaNativeProbeResult : public CPackForClient
{
	PlayerColor player;
	int32_t day = 0;
	std::string matchId, authorityId, requestId, payload, error;
	std::vector<uint8_t> checkpointBytes;
	void visitTyped(ICPackVisitor & visitor) override;
	template<typename Handler> void serialize(Handler & h)
	{
		h & player & day & matchId & authorityId & requestId & payload & error & checkpointBytes;
	}
};

struct DLL_LINKAGE ArenaDeploymentStarted : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;
	/// Owner of this private phase update. Clients reject packets for other players.
	PlayerColor player = PlayerColor::NEUTRAL;
	/// false closes the owner's deployment UI after ready/timeout.
	bool active = true;

	/// How long the player may deploy, in seconds. Comes from server-side arena config.
	int32_t durationSeconds = 0;

	/// Actual terrain of the upcoming arena battle. For ordinary battles this is
	/// server-randomized; for siege battles this is the defending town terrain.
	TerrainId nextBattleTerrain = TerrainId::NONE;
	/// Previous battle's loser and that side's current consecutive ordinary-field
	/// loss count. Neutral/zero means the first deployment, a draw, or post-siege.
	PlayerColor lastOrdinaryBattleLoser = PlayerColor::NEUTRAL;
	int32_t consecutiveOrdinaryLosses = 0;
	/// Current server-authoritative wins required for a siege. This decreases
	/// globally after each completed siege and never goes below the configured floor.
	int32_t siegeInterval = 1;
	/// True when the upcoming battle is a siege that can immediately decide the
	/// match. Clients may use this presentation-safe fact to switch AI planning
	/// from long-term investment to same-day combat power.
	bool decisiveBattle = false;
	/// True only for the player who will attack in the pending decisive siege.
	bool siegeAttacker = false;
	/// This player's server-authoritative main town. Arena siege defense always
	/// uses this town; AI must not infer it from the temporary visited town.
	ObjectInstanceID mainTown = ObjectInstanceID::NONE;
	/// Server-authoritative estimate used only to size the AI's optional reserve
	/// envelope: 0 means defending a siege now; the post-siege attacker receives
	/// the maximum safe interval.
	int32_t incomingSiegeSafetyDays = 0;
	/// Configured health ceiling for reserve-risk normalization.
	int32_t initialHealth = 1;
	/// Public-to-this-player economic schedule facts. The server remains
	/// authoritative; training AI uses these to forecast future resource grants.
	int32_t currentDay = 1;
	int32_t dailyResourceIntervalDays = 1;
	int64_t dailyResourceStage = 1;
	TResources dailyResourceBaseGrant;
	/// Both views come from the same previous BattleStart snapshot.  They are
	/// absent before the first battle and are sent only in this player's private
	/// deployment packet.
	std::optional<ArenaHeroSnapshot> previousOwnHero;
	std::optional<ArenaHeroSnapshot> previousEnemyHero;
	/// During synchronized activation, the first packet opens the deployment UI
	/// before PlayerStartsTurn. The decision authority is sent in a lightweight
	/// follow-up only after both players' deterministic day-start effects have
	/// materialized, so detached search receives the exact live RNG boundary.
	bool decisionAuthorityPending = false;
	bool decisionAuthorityOnly = false;
	/// Private server-enumerated complete unit/economy macros for one strategy
	/// decision. Empty keeps legacy clients and non-AI players lightweight.
	/// The JSON is an interface payload, not executable authority: commit still
	/// revalidates every stable action ID on the server.
	std::string completeDeploymentOptionsJson;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & active;
		h & durationSeconds;
		h & nextBattleTerrain;
		h & lastOrdinaryBattleLoser;
		h & consecutiveOrdinaryLosses;
		h & siegeInterval;
		h & decisiveBattle;
		h & siegeAttacker;
		h & mainTown;
		h & incomingSiegeSafetyDays;
		h & initialHealth;
		h & currentDay;
		h & dailyResourceIntervalDays;
		h & dailyResourceStage;
		h & dailyResourceBaseGrant;
		h & previousOwnHero;
		h & previousEnemyHero;
		h & decisionAuthorityPending;
		h & decisionAuthorityOnly;
		h & completeDeploymentOptionsJson;
	}
};

/// One combination artifact that can use a draft candidate as a constituent.
/// The server exports only the stable combination identity and its complete
/// component count; whether the hero owns other parts stays a separate,
/// current-state decision.
struct DLL_LINKAGE ArenaArtifactDraftSetOptionInfo
{
	ArtifactID artifact = ArtifactID::NONE;
	int32_t componentCount = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & artifact;
		h & componentCount;
	}
};

/// One server-authoritative candidate shown by the arena reinforcement draft.
/// Only identity and presentation-safe quantities cross the protocol; weights,
/// pools, and selection validation remain server-only.
struct DLL_LINKAGE ArenaArtifactDraftCandidateInfo
{
	bool isArtifact = true;
	ArtifactID artifact = ArtifactID::NONE;
	CreatureID creature = CreatureID::NONE;
	int32_t amount = 0;
	int32_t goldCost = 0;
	int32_t recruitLevel = 1;
	TResources cost;
	bool available = true;
	/// Complete combination recipes that contain this artifact, independent of
	/// which other components the hero currently owns.
	std::vector<ArenaArtifactDraftSetOptionInfo> artifactSetOptions;
	/// True when every compatible equipment slot is held by a combined set or
	/// one of its locked component slots. This is presentation metadata only.
	bool artifactSlotBlockedBySet = false;

	template <typename Handler> void serialize(Handler & h)
	{
		h & isArtifact;
		h & artifact;
		h & creature;
		h & amount;
		h & goldCost;
		h & recruitLevel;
		h & cost;
		h & available;
		h & artifactSetOptions;
		h & artifactSlotBlockedBySet;
	}
};

/// Private, blocking selection prompt shown before an arena deployment becomes
/// interactive. Artifact clicks are committed individually; QueryReply gives
/// up all slots that remain unfilled.
struct DLL_LINKAGE ArenaArtifactDraftSelection : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player = PlayerColor::NEUTRAL;
	bool active = true;
	QueryID queryID = QueryID::NONE;
	int32_t pickCount = 0;
	int32_t giveUpGoldPerSlot = 0;
	int32_t timeoutSeconds = 0;
	std::vector<ArenaArtifactDraftCandidateInfo> candidates;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & active;
		h & queryID;
		h & pickCount;
		h & giveUpGoldPerSlot;
		h & timeoutSeconds;
		h & candidates;
	}
};

/// ARENA: the lifecycle state of a server-authoritative disconnect countdown.
enum class EArenaDisconnectWaitStatus : int8_t
{
	WAITING = 0,
	RECONNECTED = 1,
	TIMED_OUT = 2,
};

/// ARENA: opens, updates, or closes the opponent-disconnected modal.
///
/// The server owns both elapsed-time accounting and the final loss decision.
/// Clients must only display this state and must never infer victory locally.
struct DLL_LINKAGE ArenaDisconnectWaitState : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	/// Player whose personal disconnect allowance is currently being consumed.
	PlayerColor disconnectedPlayer;

	/// Non-negative whole seconds to display. Relevant for WAITING and useful
	/// for diagnostics after RECONNECTED; the server remains authoritative.
	int32_t remainingSeconds = 0;

	EArenaDisconnectWaitStatus status = EArenaDisconnectWaitStatus::WAITING;

	template <typename Handler> void serialize(Handler & h)
	{
		h & disconnectedPlayer;
		h & remainingSeconds;
		h & status;
	}
};

/// ARENA command channel: a read-only, whitelisted diagnostic query relayed by
/// the server to exactly one client. This is not an arbitrary memory request.
struct DLL_LINKAGE ArenaCommandChannelClientRequest : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	std::string requestID;
	PlayerColor target = PlayerColor::NEUTRAL;
	std::string query;

	template <typename Handler> void serialize(Handler & h)
	{
		h & requestID;
		h & target;
		h & query;
	}
};

/// ARENA: result of an authoritative virtual town-visit request.
///
/// A virtual visit associates the main hero with a town without moving the hero's
/// map coordinates. This packet is private to the owning player. `revision` lets
/// the client discard delayed replies after a close or town switch.
enum class EArenaTownVisitStatus : int8_t
{
	ENTERED = 0,
	LEFT = 1,
	REJECTED = 2,
};

struct DLL_LINKAGE SetArenaTownVisitState : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	ObjectInstanceID mainHero;
	ObjectInstanceID town;
	uint32_t revision = 0;
	EArenaTownVisitStatus status = EArenaTownVisitStatus::REJECTED;
	/// Human-readable private failure detail. Empty on success.
	std::string reason;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & mainHero;
		h & town;
		h & revision;
		h & status;
		h & reason;
	}
};

/// ARENA: private, server-authoritative construction gate for the active town visit.
///
/// The client uses this only to render the server-selected restriction. Building
/// authorization remains a server rule. `visitRevision` prevents a delayed state
/// from a closed or switched town from affecting the current town window.
struct DLL_LINKAGE SetArenaTownBuildGateState : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	ObjectInstanceID town;
	uint32_t visitRevision = 0;
	BuildingID requiredBuilding = BuildingID::NONE;
	bool locked = false;
	TResources fortCost;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & town;
		h & visitRevision;
		h & requiredBuilding;
		h & locked;
		h & fortCost;
	}
};

/// ARENA: publishes the server-selected main hero as an absolute player-state value.
/// During face-down deployment this is normally private to the owner; battle reveal
/// may publish the actual participating hero separately.
struct DLL_LINKAGE SetArenaMainHero : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	ObjectInstanceID mainHero;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & mainHero;
	}
};

/// One server-computed option shown by the one-time recruitment-level slider.
/// `totalCost` includes both the dwelling construction and level-selection cost.
struct DLL_LINKAGE ArenaBuildLevelQuote
{
	int32_t level = 1;
	TResources totalCost;
	/// Prospective first summon cost for the post-build amount. This remains a
	/// quote only; the later town-summon request is still revalidated separately.
	TResources summonCost;
	bool summonCostValid = false;
	/// Existing main-hero stack form that becomes recruitable after this dwelling
	/// is built. This can differ from the dwelling's target creature form.
	CreatureID recruitCreature = CreatureID::NONE;
	/// Prospective recruitment cost for `recruitCreature` and this quote's amount.
	/// The later recruit request remains independently server-authoritative.
	TResources recruitCost;
	bool recruitCostValid = false;
	/// Experience threshold for the actual `recruitCreature` form. It is kept
	/// separate from `threshold`, which remains the dwelling-target summon quote.
	int64_t recruitThreshold = 0;
	/// True only when building this dwelling unlocks the stack's family and the
	/// current family experience already satisfies `recruitThreshold`.
	bool recruitAvailable = false;
	int32_t amount = 0;
	int64_t threshold = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & level;
		h & totalCost;
		h & summonCost;
		h & summonCostValid;
		h & recruitCreature;
		h & recruitCost;
		h & recruitCostValid;
		h & recruitThreshold;
		h & recruitAvailable;
		h & amount;
		h & threshold;
	}
};

enum class EArenaBuildAcquisitionKind : uint8_t
{
	SUMMON,
	RECRUIT,
};

/// Server-authoritative acquisition projection after a support building is built.
/// Capacity amounts use `growth * faction recruit-level maximum`, while `amount`
/// uses the creature family's currently selected level for an executable same-day action.
struct DLL_LINKAGE ArenaBuildAcquisitionQuote
{
	EArenaBuildAcquisitionKind kind = EArenaBuildAcquisitionKind::SUMMON;
	ObjectInstanceID town = ObjectInstanceID::NONE;
	CreatureID creature = CreatureID::NONE;
	int32_t amount = 0;
	TResources cost;
	bool available = false;
	int32_t currentCapacityAmount = 0;
	int32_t projectedCapacityAmount = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & kind;
		h & town;
		h & creature;
		h & amount;
		h & cost;
		h & available;
		h & currentCapacityAmount;
		h & projectedCapacityAmount;
	}
};

/// One whole-family removal available to a full-army dwelling package.
/// `refund` is the exact server ledger value returned by the disband action.
struct DLL_LINKAGE ArenaBuildDisbandQuote
{
	CreatureID creature = CreatureID::NONE;
	TResources refund;

	template <typename Handler> void serialize(Handler & h)
	{
		h & creature;
		h & refund;
	}
};

enum class EArenaBuildLevelStatus : int8_t
{
	/// This building may submit the family level. Base level 1 can defer the lock;
	/// the first upgraded dwelling is the final trigger.
	AVAILABLE = 0,
	/// Full quote table requested by the client.
	QUOTES = 1,
	/// Atomic building + level transaction completed.
	SUCCEEDED = 2,
	/// Request rejected. No building, resources, or level state may have changed.
	REJECTED = 3,
	/// This building is not a level trigger; remove cached controls. The family may
	/// already be locked, or a deferred choice may belong to its first upgrade.
	LOCKED = 4,
};

/// ARENA: private trigger/quote/result state for an atomic dwelling-level build.
struct DLL_LINKAGE SetArenaBuildLevelState : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	ObjectInstanceID town;
	BuildingID building;
	CreatureID creature;
	uint32_t revision = 0;
	EArenaBuildLevelStatus status = EArenaBuildLevelStatus::REJECTED;
	int32_t maxLevel = 1;
	TResources buildingCost;
	TResources buildingCostIncrease;
	std::vector<ArenaBuildLevelQuote> quotes;
	std::vector<ArenaBuildAcquisitionQuote> projectedAcquisitions;
	std::vector<ArenaBuildDisbandQuote> disbandOptions;
	std::string reason;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & town;
		h & building;
		h & creature;
		h & revision;
		h & status;
		h & maxLevel;
		h & buildingCost;
		h & buildingCostIncrease;
		h & quotes;
		h & projectedAcquisitions;
		h & disbandOptions;
		h & reason;
	}
};

/// ARENA: server-authoritative visual state for a town dwelling summon action.
enum class EArenaTownSummonStatus : uint8_t
{
	AVAILABLE,       /// Green: clicking performs the summon immediately.
	UNAVAILABLE,     /// Red: resources, army space, allowance, or another rule blocks it.
	ALREADY_IN_ARMY  /// Yellow: any creature from this upgrade family is already present.
};

/// ARENA: private, town-specific summon offer for one built dwelling.
/// All values are authoritative and already computed by the server.
struct DLL_LINKAGE SetArenaTownSummonState : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	ObjectInstanceID mainHero;
	ObjectInstanceID town;
	BuildingID building;
	CreatureID creature;
	uint32_t visitRevision = 0;
	uint32_t summonRevision = 0;
	EArenaTownSummonStatus status = EArenaTownSummonStatus::UNAVAILABLE;
	int32_t amount = 0;
	TResources cost;
	std::vector<ArenaBuildDisbandQuote> disbandOptions;
	std::string reason;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & mainHero;
		h & town;
		h & building;
		h & creature;
		h & visitRevision;
		h & summonRevision;
		h & status;
		h & amount;
		h & cost;
		h & disbandOptions;
		h & reason;
	}
};

/// ARENA: changes a player's arena health and propagates it to every client.
///
/// Server code must never write PlayerState::arenaHealth directly: doing so only
/// updates the server's own copy, leaving clients displaying a stale value.
/// Send this pack instead, so server and clients apply the same change.
///
/// This pack carries a value only. Every rule deciding how much health changes
/// stays in server/ - see section 1.1 of the technical design document.
struct DLL_LINKAGE SetArenaHealth : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::ABSOLUTE;
	PlayerColor player;
	int32_t health = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & player;
		h & health;
	}
};

struct DLL_LINKAGE SetPrimarySkill : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::RELATIVE;
	ObjectInstanceID id;
	PrimarySkill which = PrimarySkill::ATTACK;
	si64 val = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & id;
		h & which;
		h & val;
	}
};

struct DLL_LINKAGE SetHeroExperience : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::RELATIVE;
	ObjectInstanceID id;
	si64 val = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & id;
		h & val;
	}
};

struct DLL_LINKAGE GiveStackExperience : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ObjectInstanceID id;
	std::map<SlotID, si64> val;

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & val;
	}
};

struct DLL_LINKAGE SetSecSkill : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ChangeValueMode mode = ChangeValueMode::RELATIVE;
	ObjectInstanceID id;
	SecondarySkill which;
	ui16 val = 0;

	template <typename Handler> void serialize(Handler & h)
	{
		h & mode;
		h & id;
		h & which;
		h & val;
	}
};

struct DLL_LINKAGE HeroVisitCastle : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	bool startVisit = false;
	ObjectInstanceID tid;
	ObjectInstanceID hid;

	bool start() const //if hero is entering castle (if false - leaving)
	{
		return startVisit;
	}

	bool leave() const //if hero is entering castle (if false - leaving)
	{
		return !startVisit;
	}

	template <typename Handler> void serialize(Handler & h)
	{
		h & startVisit;
		h & tid;
		h & hid;
	}
};

struct DLL_LINKAGE ChangeSpells : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ui8 learn = 1; //1 - gives spell, 0 - takes
	ObjectInstanceID hid;
	std::set<SpellID> spells;

	template <typename Handler> void serialize(Handler & h)
	{
		h & learn;
		h & hid;
		h & spells;
	}
};

struct DLL_LINKAGE SetResearchedSpells : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ui8 level = 0;
	ObjectInstanceID tid;
	std::vector<SpellID> spells;
	bool accepted;

	template <typename Handler> void serialize(Handler & h)
	{
		h & level;
		h & tid;
		h & spells;
		h & accepted;
	}
};

struct DLL_LINKAGE SetMana : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	SetMana() = default;
	SetMana(ObjectInstanceID hid, si32 val, ChangeValueMode mode)
		: hid(hid)
		, val(val)
		, mode(mode)
	{}

	ObjectInstanceID hid;
	si32 val = 0;
	ChangeValueMode mode = ChangeValueMode::RELATIVE;

	template <typename Handler> void serialize(Handler & h)
	{
		h & val;
		h & hid;
		h & mode;
	}
};

struct DLL_LINKAGE SetMovePoints : public CPackForClient
{
	SetMovePoints() = default;
	SetMovePoints(ObjectInstanceID hid, si32 val)
		: hid(hid)
		, val(val)
	{}

	ObjectInstanceID hid;
	si32 val = 0;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & val;
		h & hid;
	}
};

struct DLL_LINKAGE FoWChange : public CPackForClient
{
	FowTilesType tiles;
	PlayerColor player;
	ETileVisibility mode;
	bool waitForDialogs = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & tiles;
		h & player;
		h & mode;
		h & waitForDialogs;
	}
};

struct DLL_LINKAGE SetAvailableHero : public CPackForClient
{
	SetAvailableHero()
	{
		army.clearSlots();
	}

	TavernHeroSlot slotID;
	TavernSlotRole roleID;
	PlayerColor player;
	HeroTypeID hid; //HeroTypeID::NONE if no hero
	CSimpleArmy army;
	bool replenishPoints;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & slotID;
		h & roleID;
		h & player;
		h & hid;
		h & army;
		h & replenishPoints;
	}
};

struct DLL_LINKAGE GiveBonus : public CPackForClient
{
	using VariantType = VariantIdentifier<ObjectInstanceID, PlayerColor, BattleID>;
	enum class ETarget : int8_t
	{
		OBJECT,
		PLAYER,
		BATTLE,
		HERO_COMMANDER
	};

	explicit GiveBonus(ETarget Who = ETarget::OBJECT)
		:who(Who)
	{
	}

	GiveBonus(ETarget who, const VariantType & id, const Bonus & bonus)
		: who(who)
		, id(id)
		, bonus(bonus)
	{
	}

	ETarget who = ETarget::OBJECT;
	VariantType id;
	Bonus bonus;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & bonus;
		h & id;
		h & who;
		assert(id.getNum() != -1);
	}
};

struct DLL_LINKAGE ChangeObjPos : public CPackForClient
{
	/// Object to move
	ObjectInstanceID objid;
	/// New position of visitable tile of an object
	int3 nPos;
	/// Player that initiated this action, if any
	PlayerColor initiator;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & objid;
		h & nPos;
		h & initiator;
	}
};

struct DLL_LINKAGE PlayerEndsTurn : public CPackForClient
{
	PlayerColor player;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
	}
};

struct DLL_LINKAGE PlayerEndsGame : public CPackForClient
{
	PlayerColor player;
	EVictoryLossCheckResult victoryLossCheckResult;
	StatisticDataSet statistic;
	bool silentEnd = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & victoryLossCheckResult;
		h & statistic;
		h & silentEnd;
	}
};

struct DLL_LINKAGE RemoveBonus : public CPackForClient
{
	explicit RemoveBonus(GiveBonus::ETarget Who = GiveBonus::ETarget::OBJECT)
		:who(Who)
	{
	}

	GiveBonus::ETarget who; //who receives bonus
	VariantIdentifier<HeroTypeID, PlayerColor, BattleID, ObjectInstanceID> whoID;

	//vars to identify bonus: its source
	BonusSource source;
	BonusSourceID id; //source id

	//used locally: copy of removed bonus
	Bonus bonus;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & source;
		h & id;
		h & who;
		h & whoID;
	}
};

struct DLL_LINKAGE SetCommanderProperty : public CPackForClient
{
	enum ECommanderProperty { ALIVE, BONUS, SECONDARY_SKILL, EXPERIENCE, SPECIAL_SKILL };

	ObjectInstanceID heroid;

	ECommanderProperty which = ALIVE;
	TExpType amount = 0; //0 for dead, >0 for alive
	si32 additionalInfo = 0; //for secondary skills choice
	Bonus accumulatedBonus;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & heroid;
		h & which;
		h & amount;
		h & additionalInfo;
		h & accumulatedBonus;
	}
};

struct DLL_LINKAGE AddQuest : public CPackForClient
{
	PlayerColor player;
	QuestInfo quest;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & quest;
	}
};

struct DLL_LINKAGE ChangeFormation : public CPackForClient
{
	ObjectInstanceID hid;
	EArmyFormation formation{};

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & hid;
		h & formation;
	}
};

struct DLL_LINKAGE ChangeTactics : public CPackForClient
{
	ObjectInstanceID hid;
	bool enabled = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & hid;
		h & enabled;
	}
};

struct DLL_LINKAGE ChangeTownName : public CPackForClient
{
	ObjectInstanceID tid;
	std::string name;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & tid;
		h & name;
	}
};

struct DLL_LINKAGE RemoveObject : public CPackForClient
{
	RemoveObject() = default;
	RemoveObject(const ObjectInstanceID & objectID, const PlayerColor & initiator)
		: objectID(objectID)
		, initiator(initiator)
	{
	}

	void visitTyped(ICPackVisitor & visitor) override;

	/// ID of removed object
	ObjectInstanceID objectID;

	/// Player that initiated this action, if any
	PlayerColor initiator;

	template <typename Handler> void serialize(Handler & h)
	{
		h & objectID;
		h & initiator;
	}
};

struct DLL_LINKAGE TryMoveHero : public CPackForClient
{
	enum EResult
	{
		FAILED,
		SUCCESS,
		TELEPORTATION,
		BLOCKING_VISIT,
		EMBARK,
		DISEMBARK
	};

	/// ID of moved hero
	ObjectInstanceID id;
	/// Movement points that hero will have after movement
	ui32 movePoints = 0;
	/// Result of movement attempt. FAILED should generally never happen unless client requested invalid operation
	EResult result = FAILED;
	/// Hero anchor position from which hero moves
	int3 start;
	/// Hero anchor position to which hero moves
	int3 end;
	/// Tiles that were revealed by this move
	FowTilesType fowRevealed;
	/// If hero moves on guarded tile, this field will be set to visitable pos of attacked wandering monster
	int3 attackedFrom;

	void visitTyped(ICPackVisitor & visitor) override;

	bool stopMovement() const
	{
		return result != SUCCESS && result != EMBARK && result != DISEMBARK && result != TELEPORTATION;
	}

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & result;
		h & start;
		h & end;
		h & movePoints;
		h & fowRevealed;
		h & attackedFrom;

		std::string fow;
		for (const auto & tile : fowRevealed)
			fow += tile.toString() + ", ";

		logGlobal->trace("OI %d, mp %d, res %d, start %s, end %s, attack %s, fow %s", id.getNum(), movePoints, static_cast<int>(result), start.toString(), end.toString(), attackedFrom.toString(), fow);
	}
};

struct DLL_LINKAGE NewStructures : public CPackForClient
{
	ObjectInstanceID tid;
	std::set<BuildingID> bid;
	si16 built = 0;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & tid;
		h & bid;
		h & built;
	}
};

struct DLL_LINKAGE RazeStructures : public CPackForClient
{
	ObjectInstanceID tid;
	std::set<BuildingID> bid;
	si16 destroyed = 0;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & tid;
		h & bid;
		h & destroyed;
	}
};

struct DLL_LINKAGE SetAvailableCreatures : public CPackForClient
{
	ObjectInstanceID tid;
	std::vector<std::pair<ui32, std::vector<CreatureID> > > creatures;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & tid;
		h & creatures;
	}
};

struct DLL_LINKAGE SetHeroesInTown : public CPackForClient
{
	ObjectInstanceID tid; //id of town
	ObjectInstanceID visiting; //id of visiting hero
	ObjectInstanceID garrison; //id of hero in garrison

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & tid;
		h & visiting;
		h & garrison;
	}
};

struct DLL_LINKAGE HeroRecruited : public CPackForClient
{
	HeroTypeID hid; //subID of hero
	ObjectInstanceID heroInstanceId = ObjectInstanceID::NONE;
	ObjectInstanceID tid;
	ObjectInstanceID boatId;
	int3 tile;
	PlayerColor player;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & hid;
		if(h.hasFeature(Handler::Version::ARENA_HERO_INSTANCE_ID))
			h & heroInstanceId;
		h & tid;
		h & boatId;
		h & tile;
		h & player;
	}
};

struct DLL_LINKAGE GiveHero : public CPackForClient
{
	ObjectInstanceID id; //object id
	ObjectInstanceID boatId;
	PlayerColor player;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & boatId;
		h & player;
	}
};

struct DLL_LINKAGE OpenWindow : public Query
{
	EOpenWindowMode window;
	ObjectInstanceID object;
	ObjectInstanceID visitor;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & window;
		h & object;
		h & visitor;
	}
};

struct DLL_LINKAGE NewObject : public CPackForClient
{
	/// Object ID to create
	std::shared_ptr<CGObjectInstance> newObject;
	/// Which player initiated creation of this object
	PlayerColor initiator;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & newObject;
		h & initiator;
	}
};

struct DLL_LINKAGE SetAvailableArtifacts : public CPackForClient
{
	//two variants: id < 0: set artifact pool for Artifact Merchants in towns; id >= 0: set pool for adv. map Black Market (id is the id of Black Market instance then)
	ObjectInstanceID id;
	std::vector<ArtifactID> arts;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & arts;
	}
};

struct DLL_LINKAGE CGarrisonOperationPack : CPackForClient
{
};

struct DLL_LINKAGE ChangeStackCount : CGarrisonOperationPack
{
	ObjectInstanceID army;
	SlotID slot;
	TQuantity count;
	ChangeValueMode mode = ChangeValueMode::RELATIVE;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & army;
		h & slot;
		h & count;
		h & mode;
	}
};

struct DLL_LINKAGE SetStackType : CGarrisonOperationPack
{
	ObjectInstanceID army;
	SlotID slot;
	CreatureID type;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & army;
		h & slot;
		h & type;
	}
};

struct DLL_LINKAGE EraseStack : CGarrisonOperationPack
{
	ObjectInstanceID army;
	SlotID slot;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & army;
		h & slot;
	}
};

struct DLL_LINKAGE SwapStacks : CGarrisonOperationPack
{
	ObjectInstanceID srcArmy;
	ObjectInstanceID dstArmy;
	SlotID srcSlot;
	SlotID dstSlot;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & srcArmy;
		h & dstArmy;
		h & srcSlot;
		h & dstSlot;
	}
};

struct DLL_LINKAGE InsertNewStack : CGarrisonOperationPack
{
	ObjectInstanceID army;
	SlotID slot;
	CreatureID type;
	TQuantity count = 0;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & army;
		h & slot;
		h & type;
		h & count;
	}
};

/// Private presentation of resources granted at the opening of an arena day.
/// The resource mutation itself is sent separately through SetResources.
struct DLL_LINKAGE ArenaDailyResourceGrant : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	TResources resources;
	int32_t durationSeconds = 0;
	bool active = true;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & resources;
		h & durationSeconds;
		h & active;
	}
};

struct DLL_LINKAGE SetStackArenaInitialUnit : CGarrisonOperationPack
{
	ObjectInstanceID army;
	SlotID slot;
	bool initialUnit = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & army;
		h & slot;
		h & initialUnit;
	}
};

///moves creatures from src stack to dst slot, may be used for merging/splittint/moving stacks
struct DLL_LINKAGE RebalanceStacks : CGarrisonOperationPack
{
	ObjectInstanceID srcArmy;
	ObjectInstanceID dstArmy;
	SlotID srcSlot;
	SlotID dstSlot;

	TQuantity count;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & srcArmy;
		h & dstArmy;
		h & srcSlot;
		h & dstSlot;
		h & count;
	}
};

struct DLL_LINKAGE BulkRebalanceStacks : CGarrisonOperationPack
{
	std::vector<RebalanceStacks> moves;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler>
	void serialize(Handler & h)
	{
		h & moves;
	}
};

struct DLL_LINKAGE CArtifactOperationPack : CPackForClient
{
};

struct DLL_LINKAGE GrowUpArtifact : CArtifactOperationPack
{
	ArtifactInstanceID id;

	GrowUpArtifact() = default;
	GrowUpArtifact(const ArtifactInstanceID & id)
		: id(id)
	{
	}

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
	}
};

struct DLL_LINKAGE PutArtifact : CArtifactOperationPack
{
	PutArtifact() = default;
	explicit PutArtifact(const ArtifactInstanceID & id, const ArtifactLocation & dst, bool askAssemble = true)
		: al(dst), askAssemble(askAssemble), id(id)
	{
	}

	ArtifactLocation al;
	bool askAssemble;
	ArtifactInstanceID id;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & al;
		h & askAssemble;
		h & id;
	}
};

struct DLL_LINKAGE NewArtifact : public CArtifactOperationPack
{
	ObjectInstanceID artHolder;
	ArtifactID artId;
	SpellID spellId;
	ArtifactPosition pos;
	/// Server-authoritative instance ID. Private arena deployment packets can
	/// leave gaps in another client's registry without exposing the artifact.
	ArtifactInstanceID instanceId;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & artHolder;
		h & artId;
		h & spellId;
		h & pos;
		h & instanceId;
	}
};

struct DLL_LINKAGE BulkEraseArtifacts : CArtifactOperationPack
{
	ObjectInstanceID artHolder;
	std::vector<ArtifactPosition> posPack;
	std::optional<SlotID> creature;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & artHolder;
		h & posPack;
		h & creature;
	}
};

struct DLL_LINKAGE BulkMoveArtifacts : CArtifactOperationPack
{
	PlayerColor interfaceOwner;
	ObjectInstanceID srcArtHolder;
	ObjectInstanceID dstArtHolder;
	std::optional<SlotID> srcCreature;
	std::optional<SlotID> dstCreature;

	BulkMoveArtifacts()
		: interfaceOwner(PlayerColor::NEUTRAL)
		, srcArtHolder(ObjectInstanceID::NONE)
		, dstArtHolder(ObjectInstanceID::NONE)
		, srcCreature(std::nullopt)
		, dstCreature(std::nullopt)
	{
	}
	BulkMoveArtifacts(const PlayerColor & interfaceOwner, const ObjectInstanceID srcArtHolder, const ObjectInstanceID dstArtHolder, bool swap)
		: interfaceOwner(interfaceOwner)
		, srcArtHolder(srcArtHolder)
		, dstArtHolder(dstArtHolder)
		, srcCreature(std::nullopt)
		, dstCreature(std::nullopt)
	{
	}

	std::vector<MoveArtifactInfo> artsPack0;
	std::vector<MoveArtifactInfo> artsPack1;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & interfaceOwner;
		h & artsPack0;
		h & artsPack1;
		h & srcArtHolder;
		h & dstArtHolder;
		h & srcCreature;
		h & dstCreature;
	}
};

struct DLL_LINKAGE DischargeArtifact : CArtifactOperationPack
{
	ArtifactInstanceID id;
	uint16_t charges;
	std::optional<ArtifactLocation> artLoc;

	DischargeArtifact() = default;
	DischargeArtifact(const ArtifactInstanceID & id, const uint16_t charges)
		: id(id)
		, charges(charges)
	{
	}

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & charges;
		h & artLoc;
	}
};

struct DLL_LINKAGE AssembledArtifact : CArtifactOperationPack
{
	ArtifactLocation al;
	ArtifactID artId;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & al;
		h & artId;
	}
};

struct DLL_LINKAGE DisassembledArtifact : CArtifactOperationPack
{
	ArtifactLocation al;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & al;
	}
};

struct DLL_LINKAGE HeroVisit : public CPackForClient
{
	PlayerColor player;
	ObjectInstanceID heroId;
	ObjectInstanceID objId;

	bool starting; //false -> ending

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & heroId;
		h & objId;
		h & starting;
	}
};

struct DLL_LINKAGE InfoWindow : public CPackForClient //103  - displays simple info window
{
	EInfoWindowMode type = EInfoWindowMode::MODAL;
	MetaString text;
	std::vector<Component> components;
	PlayerColor player;
	ui16 soundID = 0;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & type;
		h & text;
		h & components;
		h & player;
		h & soundID;
	}
	InfoWindow() = default;
};

struct DLL_LINKAGE NewTurn : public CPackForClient
{
	void visitTyped(ICPackVisitor & visitor) override;

	ui32 day = 0;
	CreatureID creatureid; //for creature weeks
	EWeekType specialWeek = EWeekType::NORMAL;

	std::vector<SetMovePoints> heroesMovement;
	std::vector<SetMana> heroesMana;
	std::vector<SetAvailableCreatures> availableCreatures;
	std::map<PlayerColor, ResourceSet> playerIncome;
	std::optional<RumorState> newRumor; // only on new weeks
	std::optional<InfoWindow> newWeekNotification; // only on new week

	NewTurn() = default;

	template <typename Handler> void serialize(Handler & h)
	{
		h & day;
		h & creatureid;
		h & specialWeek;
		h & heroesMovement;
		h & heroesMana;
		h & availableCreatures;
		h & playerIncome;
		h & newRumor;
		h & newWeekNotification;
	}
};

struct DLL_LINKAGE SetObjectProperty : public CPackForClient
{
	ObjectInstanceID id;
	ObjProperty what{};

	ObjPropertyID identifier;

	SetObjectProperty() = default;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & what;
		h & identifier;
	}
};

struct DLL_LINKAGE ChangeObjectVisitors : public CPackForClient
{
	enum VisitMode
	{
		VISITOR_ADD_HERO,   // mark hero as one that have visited this object
		VISITOR_ADD_PLAYER, // mark player as one that have visited this object instance
		VISITOR_SCOUTED,    // marks targeted team as having scouted this object
		VISITOR_CLEAR,      // clear all visitors from this object (object reset)
	};
	VisitMode mode = VISITOR_CLEAR; // uses VisitMode enum
	ObjectInstanceID object;
	ObjectInstanceID hero; // note: hero owner will be also marked as "visited" this object

	void visitTyped(ICPackVisitor & visitor) override;

	ChangeObjectVisitors() = default;

	ChangeObjectVisitors(VisitMode mode, const ObjectInstanceID & object, const ObjectInstanceID & heroID = ObjectInstanceID(-1))
		: mode(mode)
		, object(object)
		, hero(heroID)
	{
	}

	template <typename Handler> void serialize(Handler & h)
	{
		h & object;
		h & hero;
		h & mode;
	}
};

struct DLL_LINKAGE ChangeArtifactsCostume : public CPackForClient
{
	std::map<ArtifactPosition, ArtifactID> costumeSet;
	uint32_t costumeIdx = 0;
	const PlayerColor player = PlayerColor::NEUTRAL;

	void visitTyped(ICPackVisitor & visitor) override;

	ChangeArtifactsCostume() = default;
	ChangeArtifactsCostume(const PlayerColor & player, const uint32_t costumeIdx)
		: costumeIdx(costumeIdx)
		, player(player)
	{
	}

	template <typename Handler> void serialize(Handler & h)
	{
		h & costumeSet;
		h & costumeIdx;
		h & player;
	}
};

struct DLL_LINKAGE HeroLevelUp : public Query
{
	PlayerColor player;
	ObjectInstanceID heroId;

	PrimarySkill primskill = PrimarySkill::ATTACK;
	std::vector<SecondarySkill> skills;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & player;
		h & heroId;
		h & primskill;
		h & skills;
	}
};

struct DLL_LINKAGE CommanderLevelUp : public Query
{
	PlayerColor player;
	ObjectInstanceID heroId;

	std::vector<ui32> skills; //0-5 - secondary skills, val-100 - special skill

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & player;
		h & heroId;
		h & skills;
	}
};

//A dialog that requires making decision by player - it may contain components to choose between or has yes/no options
//Client responds with QueryReply, where answer: 0 - cancel pressed, choice doesn't matter; 1/2/...  - first/second/... component selected and OK pressed
//Until sending reply player won't be allowed to take any actions
struct DLL_LINKAGE BlockingDialog : public Query
{
	enum { ALLOW_CANCEL = 1, SELECTION = 2, SAFE_TO_AUTOACCEPT = 4 };
	MetaString text;
	std::vector<Component> components;
	PlayerColor player;
	ui8 flags = 0;
	ui16 soundID = 0;

	bool cancel() const
	{
		return flags & ALLOW_CANCEL;
	}
	bool selection() const
	{
		return flags & SELECTION;
	}

	bool safeToAutoaccept() const
	{
		return flags & SAFE_TO_AUTOACCEPT;
	}

	BlockingDialog(bool yesno, bool Selection)
	{
		if(yesno) flags |= ALLOW_CANCEL;
		if(Selection) flags |= SELECTION;
	}
	BlockingDialog() = default;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & text;
		h & components;
		h & player;
		h & flags;
		h & soundID;
	}
};

struct DLL_LINKAGE GarrisonDialog : public Query
{
	ObjectInstanceID objid;
	ObjectInstanceID hid;
	bool removableUnits = false;
	MetaString customTitle;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & objid;
		h & hid;
		h & removableUnits;
		if (h.hasFeature(Handler::Version::CUSTOM_GARRISON_TITLE))
			h & customTitle;
	}
};

struct DLL_LINKAGE ExchangeDialog : public Query
{
	PlayerColor player;

	ObjectInstanceID hero1;
	ObjectInstanceID hero2;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & player;
		h & hero1;
		h & hero2;
	}
};

struct DLL_LINKAGE TeleportDialog : public Query
{
	TeleportDialog() = default;

	TeleportDialog(const ObjectInstanceID & hero, const TeleportChannelID & Channel)
		: hero(hero)
		, channel(Channel)
	{
	}
	ObjectInstanceID hero;
	TeleportChannelID channel;
	TTeleportExitsList exits;
	bool impassable = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & hero;
		h & channel;
		h & exits;
		h & impassable;
	}
};

struct DLL_LINKAGE MapObjectSelectDialog : public Query
{
	PlayerColor player;
	Component icon;
	MetaString title;
	MetaString description;
	std::vector<ObjectInstanceID> objects;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & queryID;
		h & player;
		h & icon;
		h & title;
		h & description;
		h & objects;
	}
};

struct DLL_LINKAGE AdvmapSpellCast : public CPackForClient
{
	ObjectInstanceID casterID;
	SpellID spellID;
	template <typename Handler> void serialize(Handler & h)
	{
		h & casterID;
		h & spellID;
	}

protected:
	void visitTyped(ICPackVisitor & visitor) override;
};

struct DLL_LINKAGE ShowWorldViewEx : public CPackForClient
{
	PlayerColor player;
	bool showTerrain; // TODO: send terrain state

	std::vector<ObjectPosInfo> objectPositions;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & showTerrain;
		h & objectPositions;
	}

protected:
	void visitTyped(ICPackVisitor & visitor) override;
};

struct DLL_LINKAGE PlayerMessageClient : public CPackForClient
{
	PlayerMessageClient() = default;
	PlayerMessageClient(const PlayerColor & Player, std::string Text)
		: player(Player)
		, text(std::move(Text))
	{
	}
	void visitTyped(ICPackVisitor & visitor) override;

	PlayerColor player;
	std::string text;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & text;
	}
};

struct DLL_LINKAGE CenterView : public CPackForClient
{
	PlayerColor player;
	int3 pos;
	ui32 focusTime = 0; //ms

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & pos;
		h & player;
		h & focusTime;
	}
};

struct DLL_LINKAGE ResponseStatistic : public CPackForClient
{
	PlayerColor player;
	StatisticDataSet statistic;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & statistic;
	}
};

VCMI_LIB_NAMESPACE_END
