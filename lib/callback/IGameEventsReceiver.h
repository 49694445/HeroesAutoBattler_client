/*
 * IGameEventsReceiver.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include "../gameState/GameStatistics.h"
#include "../int3.h"

VCMI_LIB_NAMESPACE_BEGIN

struct ArtifactLocation;
struct Bonus;
struct Component;
struct CPackForServer;
struct PackageApplied;
struct SetObjectProperty;
struct TryMoveHero;
struct ArenaDeploymentStarted;
struct ArenaNativeProbeResult;
struct ArenaArtifactDraftSelection;
struct ArenaDailyResourceGrant;
struct SetArenaTownVisitState;
struct SetArenaTownBuildGateState;
struct SetArenaBuildLevelState;
struct SetArenaTownSummonState;

class CArmedInstance;
class CCreatureSet;
class CGBlackMarket;
class CGDwelling;
class CGHeroInstance;
class CGObjectInstance;
class CGTownInstance;
class EVictoryLossCheckResult;
class IShipyard;
class IMarket;

using FowTilesType = std::set<int3>;
enum class EInfoWindowMode : uint8_t;

/// Sanitized, public battle-progress state. It intentionally carries no
/// recruitment level, exact experience balance, resources or town data.
enum class EArenaRecruitmentThresholdState : uint8_t
{
	UNKNOWN,
	BELOW,
	READY
};

struct ArenaRecruitmentThresholdObservation
{
	CreatureID creature = CreatureID::NONE;
	EArenaRecruitmentThresholdState state = EArenaRecruitmentThresholdState::UNKNOWN;
};

class DLL_LINKAGE IGameEventsReceiver
{
public:
	virtual void buildChanged(const CGTownInstance *town, BuildingID buildingID, int what){}; //what: 1 - built, 2 - demolished

	virtual void battleResultsApplied(){}; //called when all effects of last battle are applied
	virtual void battleEnded(){}; //called when a battle has ended

	virtual void garrisonsChanged(ObjectInstanceID id1, ObjectInstanceID id2){};

	//artifacts operations
	virtual void artifactPut(const ArtifactLocation &al){};
	virtual void artifactRemoved(const ArtifactLocation &al){};
	virtual void artifactAssembled(const ArtifactLocation &al){};
	virtual void artifactDisassembled(const ArtifactLocation &al){};
	virtual void artifactMoved(const ArtifactLocation &src, const ArtifactLocation &dst){};
	virtual void bulkArtMovementStart(size_t totalNumOfArts, size_t possibleAssemblyNumOfArts) {};
	virtual void askToAssembleArtifact(const ArtifactLocation & dst) {};

	virtual void heroVisit(const CGHeroInstance *visitor, const CGObjectInstance *visitedObj, bool start){};
	virtual void heroCreated(const CGHeroInstance*){};
	virtual void heroInGarrisonChange(const CGTownInstance *town){};
	virtual void heroMoved(const TryMoveHero & details, bool verbose = true){};
	virtual void heroExperienceChanged(const CGHeroInstance * hero, si64 val){};
	virtual void heroPrimarySkillChanged(const CGHeroInstance * hero, PrimarySkill which, si64 val){};
	virtual void heroSecondarySkillChanged(const CGHeroInstance * hero, int which, int val){};
	virtual void heroManaPointsChanged(const CGHeroInstance * hero){} //not called at the beginning of turn and after spell casts
	virtual void heroMovePointsChanged(const CGHeroInstance * hero){} //not called at the beginning of turn and after movement
	virtual void heroVisitsTown(const CGHeroInstance* hero, const CGTownInstance * town){};
	virtual void receivedResource(){};
	virtual void showInfoDialog(EInfoWindowMode type, const std::string & text, const std::vector<Component> & components, int soundID){};
	virtual void showRecruitmentDialog(const CGDwelling *dwelling, const CArmedInstance *dst, int level, QueryID queryID){}
	virtual void showShipyardDialog(const IShipyard *obj){} //obj may be town or shipyard; state: 0 - can build, 1 - lack of resources, 2 - dest tile is blocked, 3 - no water

	virtual void showPuzzleMap(){};
	virtual void viewWorldMap(){};
	virtual void showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID){};
	virtual void showUniversityWindow(const IMarket *market, const CGHeroInstance *visitor, QueryID queryID){};
	virtual void showHillFortWindow(const CGObjectInstance *object, const CGHeroInstance *visitor){};
	virtual void showThievesGuildWindow (const CGObjectInstance * obj){};
	virtual void showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QueryID queryID) {};
	virtual void showQuestLog(){};
	virtual void advmapSpellCast(const CGHeroInstance * caster, SpellID spellID){}; //called when a hero casts a spell
	virtual void tileHidden(const FowTilesType &pos){};
	virtual void tileRevealed(const FowTilesType &pos){};
	virtual void newObject(const CGObjectInstance * obj){}; //eg. ship built in shipyard
	virtual void availableArtifactsChanged(const CGBlackMarket *bm = nullptr){}; //bm may be nullptr, then artifacts are changed in the global pool (used by merchants in towns)
	virtual void centerView (int3 pos, int focusTime){};
	virtual void availableCreaturesChanged(const CGDwelling *town){};
	virtual void heroBonusChanged(const CGHeroInstance *hero, const Bonus &bonus, bool gain){};//if gain hero received bonus, else he lost it
	virtual void playerBonusChanged(const Bonus &bonus, bool gain){};//if gain hero received bonus, else he lost it
	virtual void requestSent(const CPackForServer *pack, int requestID){};
	virtual void requestRealized(PackageApplied *pa){};
	virtual void beforeObjectPropertyChanged(const SetObjectProperty * sop){}; //eg. mine has been flagged
	virtual void objectPropertyChanged(const SetObjectProperty * sop){}; //eg. mine has been flagged
	virtual void objectRemoved(const CGObjectInstance *obj, const PlayerColor & initiator){}; //eg. collected resource, picked artifact, beaten hero
	virtual void objectRemovedAfter(){}; //eg. collected resource, picked artifact, beaten hero
	virtual void playerBlocked(int reason, bool start){}; //reason: 0 - upcoming battle
	virtual void gameOver(PlayerColor player, const EVictoryLossCheckResult & victoryLossCheckResult) {}; //player lost or won the game
	virtual void playerStartsTurn(PlayerColor player){};
	virtual void playerEndsTurn(PlayerColor player){};
	/// Battle-only deployment planners may be separate from the combat AI. Give
	/// them the already revealed, authoritative armies and physical hero factors
	/// for next-round planning without installing a second battle controller.
	/// The factors currently are CGHeroInstance::getFightingStrength() only
	/// (attack + defense); skills, artifacts and spells must use future fields.
	virtual void arenaBattleStartedObservation(const CCreatureSet * ownArmy, const CCreatureSet * enemyArmy,
		double ownHeroAttackDefenseFightingStrength, double enemyHeroAttackDefenseFightingStrength){};
	/// Battle-only deployment planners are separate from combat AI. Publish the
	/// authoritative result to the owning player interface without calling a
	/// second battle controller.
	virtual void arenaBattleResultObservation(bool won, const std::map<CreatureID, si32> & enemyCasualties){};

	// Arena deployment protocol events. AI implementations use these private,
	// server-authored offers instead of duplicating arena rules client-side.
	virtual void arenaDeploymentState(const ArenaDeploymentStarted &){};
	virtual void arenaNativeProbeResult(const ArenaNativeProbeResult &) {};
	virtual void arenaArtifactDraftState(const ArenaArtifactDraftSelection &){};
	virtual void arenaDailyResourceState(const ArenaDailyResourceGrant &){};
	virtual void arenaTownVisitState(const SetArenaTownVisitState &){};
	virtual void arenaTownBuildGateState(const SetArenaTownBuildGateState &){};
	virtual void arenaBuildLevelState(const SetArenaBuildLevelState &){};
	virtual void arenaTownSummonState(const SetArenaTownSummonState &){};

	virtual void responseStatistic(StatisticDataSet & statistic){};

	//TODO shouldn't be moved down the tree?
	virtual void heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID queryID){};

	/// Delivered after a battle result from the latest progress snapshot that was
	/// already public in the battle UI. BELOW describes that published snapshot,
	/// not an unseen post-battle private balance; missing/invalid data is UNKNOWN.
	virtual void arenaBattleRecruitmentThresholdObservation(
		const std::vector<ArenaRecruitmentThresholdObservation> & enemyThresholdStates){};
};

VCMI_LIB_NAMESPACE_END
