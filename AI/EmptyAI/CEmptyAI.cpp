/*
 * CEmptyAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CEmptyAI.h"

#include "../../lib/CRandomGenerator.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/networkPacks/PacksForClient.h"

void CEmptyAI::initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB)
{
	cb = CB;
	env = ENV;
	human=false;
	playerID = *cb->getPlayerID();
}

void CEmptyAI::yourTurn(QueryID queryID)
{
	cb->selectionMade(0, queryID);
	cb->endTurn();
}

void CEmptyAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));
}

void CEmptyAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
}

void CEmptyAI::heroGotLevel(const CGHeroInstance *hero, PrimarySkill pskill, std::vector<SecondarySkill> &skills, QueryID queryID)
{
	cb->selectionMade(CRandomGenerator::getDefault().nextInt((int)skills.size() - 1), queryID);
}

void CEmptyAI::commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID)
{
	cb->selectionMade(CRandomGenerator::getDefault().nextInt((int)skills.size() - 1), queryID);
}

void CEmptyAI::showBlockingDialog(const std::string &text, const std::vector<Component> &components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept)
{
	cb->selectionMade(0, askID);
}

void CEmptyAI::showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID)
{
	cb->selectionMade(0, askID);
}

void CEmptyAI::showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle)
{
	cb->selectionMade(0, queryID);
}

void CEmptyAI::showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects)
{
	cb->selectionMade(0, askID);
}

std::optional<BattleAction> CEmptyAI::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	return std::nullopt;
}

void CServerDrivenArenaAI::yourTurn(QueryID queryID)
{
	// These callbacks run on the network thread. Never wait for a reply here:
	// the default CCallback sends asynchronously, preserving request order.
	if(queryID != QueryID::NONE)
		cb->selectionMade(0, queryID);
	turnAcknowledged = true;
	tryTestReady();
	// In particular, do not call EmptyAI::yourTurn: its endTurn would bypass
	// the deployment phase. The server FSM owns the AI player's decisions.
}

bool CServerDrivenArenaAI::autoReadyTestPlayer() const
{
	const auto * player = cb->getPlayerState(playerID, false);
	return settings["session"]["headless"].Bool()
		&& settings["session"]["testArenaAutoReady"].Bool()
		&& player && player->isHuman();
}

void CServerDrivenArenaAI::tryTestReady()
{
	if(!autoReadyTestPlayer() || !deploymentActive || !turnAcknowledged
		|| !draftResolved || readyDay == deploymentDay)
		return;
	readyDay = deploymentDay;
	logAi->info("[ARENA-TEST] auto-ready human player=%d day=%d", playerID.getNum(), deploymentDay);
	cb->arenaDeploymentReady();
}

void CServerDrivenArenaAI::arenaDeploymentState(const ArenaDeploymentStarted & pack)
{
	if(pack.player != playerID)
		return;
	if(pack.decisionAuthorityOnly)
	{
		// No strategy, weights, or private observations are needed client-side.
		logAi->info("[ARENA-CLIENT] player=%d day=%d deployment authority received; local deployment AI disabled",
			playerID.getNum(), pack.currentDay);
		return;
	}
	if(pack.active && pack.currentDay != deploymentDay)
	{
		deploymentDay = pack.currentDay;
		turnAcknowledged = false;
		draftResolved = deploymentDay == 1;
	}
	deploymentActive = pack.active;
	tryTestReady();
}

void CServerDrivenArenaAI::arenaArtifactDraftState(const ArenaArtifactDraftSelection & pack)
{
	// Production AI draft choices belong entirely to the server. This only
	// preserves the explicit localhost headless test's human-seat automation.
	if(pack.player != playerID || !autoReadyTestPlayer() || !deploymentActive)
		return;
	if(pack.active)
	{
		draftResolved = false;
		cb->sendQueryReply(0, pack.queryID);
	}
	else
	{
		draftResolved = true;
		tryTestReady();
	}
}

void CServerDrivenArenaAI::gameOver(PlayerColor player, const EVictoryLossCheckResult & result)
{
	deploymentActive = false;
	logAi->info("[ARENA-CLIENT] game-over received player=%d observer=%d", player.getNum(), playerID.getNum());
}
