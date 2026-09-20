/*
 * AutocombatPreferences.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

struct AutocombatPreferences
{
	bool enableSpellsUsage = true;
	// Used by the in-battle "finish with autocombat" command. This may only
	// remove diagnostic/allocation overhead; it must not change AI decisions.
	bool losslessFastResolve = false;
	//TODO: below options exist in original H3, consider usefulness of mixed human-AI combat when enabling autocombat inside battle
//	bool enableUnitsUsage = true;
//	bool enableCatapultUsage = true;
//	bool enableBallistaUsage = true;
//	bool enableFirstAidTendUsage = true;
};

