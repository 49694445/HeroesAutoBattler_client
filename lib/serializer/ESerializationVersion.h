/*
 * ESerializationVersion.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/// This enumeration controls save compatibility support.
/// - 'MINIMAL' represents the oldest supported version counter. A saved game can be loaded if its version is at least 'MINIMAL'.
/// - 'CURRENT' represents the current save version. Saved games are created using the 'CURRENT' version.
///
/// To make a save-breaking change:
/// - change 'MINIMAL' to a value higher than 'CURRENT'
/// - remove all keys in enumeration between 'MINIMAL' and 'CURRENT' as well as all their usage (will be detected by compiler)
/// - change 'CURRENT' to 'CURRENT = MINIMAL'
///
/// To make a non-breaking change:
/// - add new enumeration value before 'CURRENT'
/// - change 'CURRENT' to 'CURRENT = NEW_TEST_KEY'.
///
/// To check for version in serialize() call use form
/// if (h.hasFeature(Handler::Version::NEW_TEST_KEY))
///     h & newKey; // loading/saving save of a new version
/// else
///     newKey = saneDefaultValue; // loading of old save
enum class ESerializationVersion : int32_t
{
	NONE = 0,

	RELEASE_160 = 873,
	MINIMAL = RELEASE_160,

	MAP_HEADER_DISPOSED_HEROES, // map header contains disposed heroes list
	NO_RAW_POINTERS_IN_SERIALIZER, // large rework that removed all non-owning pointers from serializer
	STACK_INSTANCE_EXPERIENCE_FIX, // stack experience is stored as total, not as average
	STACK_INSTANCE_ARMY_FIX, // remove serialization of army that owns stack instance
	STORE_UID_COUNTER_IN_CMAP,  // fix crash caused by conflicting instanceName after loading game
	REWARDABLE_EXTENSIONS, // new functionality for rewardable objects
	FLAGGABLE_BONUS_SYSTEM_NODE, // flaggable objects now contain bonus system node
	RANDOMIZATION_REWORK, // random rolls logic has been moved to server
	CUSTOM_BONUS_ICONS, // support for custom icons in bonuses
	SERVER_STATISTICS, // statistics now only saved on server
	OPPOSITE_SIDE_LIMITER_OWNER, // opposite side limiter no longer stores owner in itself
	UNIVERSITY_CONFIG, // town university is configurable
	CAMPAIGN_BONUSES, // new format for scenario bonuses in campaigns
	BONUS_HIDDEN, // hidden bonus
	MORE_MAP_LAYERS, // more map layers
	CONFIGURABLE_RESOURCES, // configurable resources
	CUSTOM_NAMES, // custom names
	BATTLE_ONLY, // battle only mode
	CAMPAIGN_VIDEO, // second video for prolog/epilog in campaigns
	HOTA_MAP_STACK_COUNT, // support Hota 1.7 stack count feature
	HOTA_MAP_FORMAT_EXTENSIONS, // support multiple Hota 1.7 map format features
	SPELL_RESEARCH_IMPROVEMENTS, // support counting past spell rerolls
	NAME_MAP_LAYERS, // name map layers
	HOTA_MAP_FORMAT_EXTENSIONS_2, // more Hota 1.7 map format features
	TIMER_MOVEMENT_POINTS, // movement points for timer
	DISABLE_TACTICS, // disable tactics
	REWARDABLE_EXTENSIONS_2, // movement points limiter for rewardables
	BONUS_TRIGGER, // bonus that allows triggered effects in combat
	CUSTOM_GARRISON_TITLE, // GarrisonDialog pack now has custom title parameter
	ARENA_CREATURE_SPENDING_LEDGER, // server saves cumulative recruit, summon and upgrade costs by creature family
	ARENA_SIEGE_STREAK_STATE, // server saves ordinary-win streaks and pending/active arena siege state
	ARENA_NEXT_BATTLE_TERRAIN, // server saves the selected terrain across arena day-start reloads
	ARENA_HERO_LEVEL_UP_TIMEOUT, // server saves in-progress arena hero level-up choice timers
	ARENA_HERO_LEVEL_UP_TIMEOUT_FLOW, // server saves arena hero level-up timer flow lifecycle
	ARENA_BATTLE_RESULT_TIMEOUT, // server saves arena battle-result acknowledgement timers
	ARENA_INITIAL_UNIT, // stack stores whether it belongs to the arena hero's untouched starting army
	ARENA_MULTI_HERO, // server saves hidden recruited heroes and pending main-hero artifact reveals
	ARENA_HERO_INSTANCE_ID, // hidden hero recruitment preserves the authoritative map object ID
	ARENA_ROUND_STATE, // server saves the arena round used by round-dependent draft rules
	ARENA_PREVIOUS_BATTLE_HERO_SNAPSHOTS, // server saves detached public hero views for the next deployment UI
	ARENA_DYNAMIC_SIEGE_INTERVAL, // server saves the global completed-siege count used by the decreasing interval
	ARENA_RULE_PROFILE_LINEAGE, // server saves the immutable rule-profile fingerprint selected before match initialization
	ARENA_DEPLOYMENT_CHECKPOINT, // server saves an in-progress deployment and detached formal-battle handoff barrier

	RELEASE_170 = HOTA_MAP_STACK_COUNT,
	RELEASE_174 = CUSTOM_GARRISON_TITLE,
	CURRENT = ARENA_DEPLOYMENT_CHECKPOINT,
};

static_assert(ESerializationVersion::MINIMAL <= ESerializationVersion::CURRENT, "Invalid serialization version definition!");
