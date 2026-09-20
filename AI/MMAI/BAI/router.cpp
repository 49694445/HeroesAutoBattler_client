/*
 * router.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CRandomGenerator.h"
#include "callback/CBattleCallback.h"
#include "callback/CDynLibHandler.h"
#include "callback/IGameInfoCallback.h"
#include "battle/BattleInfo.h"
#include "filesystem/Filesystem.h"
#include "json/JsonUtils.h"

#include "BAI/base.h"
#include "BAI/model/NNModel.h"
#include "BAI/model/NNModelStochastic.h"
#include "BAI/model/ScriptedModel.h"
#include "BAI/router.h"

#include "common.h"

#include <utility>

namespace MMAI::BAI
{
using ModelStorage = std::map<std::string, std::unique_ptr<Schema::IModel>>;

namespace
{
	struct ModelRepository
	{
		ModelStorage models;
		float temperature = 1.0;
		int maxPredictions = 100;
		uint64_t seed = 0;
		bool randomSeedConfigured = false;
		std::unique_ptr<ScriptedModel> fallbackModel;
		std::string fallbackName;
	};

	std::unique_ptr<ModelRepository> InitModelRepository()
	{
		auto repo = std::make_unique<ModelRepository>();
		auto json = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
		if(!json.isStruct())
		{
			logAi->error("Could not load MMAI config. Is MMAI mod enabled?");
			std::string fallback = "BattleAI";
			logAi->debug("MMAI: preparing fallback model: %s", fallback);
			repo->fallbackModel = std::make_unique<ScriptedModel>(fallback);
			repo->fallbackName = fallback;
			return repo;
		}

		JsonUtils::validate(json, "vcmi:mmaiSettings", "mmai");
		repo->temperature = static_cast<float>(json["temperature"].Float());
		if(!json["maxPredictions"].isNull())
			repo->maxPredictions = static_cast<int>(json["maxPredictions"].Integer());
		if(repo->maxPredictions < 1 || repo->maxPredictions > 100000)
			throw std::runtime_error("MMAI maxPredictions must be between 1 and 100000");

		repo->seed = json["seed"].Integer();
		repo->randomSeedConfigured = repo->seed == 0;
		if(repo->randomSeedConfigured)
			repo->seed = CRandomGenerator::getDefault().nextInt();

		for(const std::string key : {"attacker", "defender"})
		{
			std::string path = "MMAI/models/" + json["models"][key].String();

			// Try loading stochastic and dynamic models with priority
			// (temporary code for a smooth migration path)
			std::string suffix;
			const auto pos = path.rfind(".onnx");
			if(pos != std::string::npos)
			{
				for(const std::string s : {"stochastic", "dynamic"})
				{
					std::string altpath = path;
					altpath.insert(pos, "-" + s); // insert right before ".onnx"
					const auto rpath = ResourcePath(altpath, EResType::AI_MODEL);
					const auto * rhandler = CResourceHandler::get();
					if(rhandler->existsResource(rpath))
					{
						path = altpath;
						suffix = s;
						break;
					}
				}
			}

			logAi->debug("MMAI: Loading NN %s model from: %s", key, path);
			try
			{
				// Only stochastic models use a separate class
				if(suffix == "stochastic")
					repo->models.try_emplace(key, std::make_unique<NNModelStochastic>(path, repo->temperature, repo->seed));
				else
					repo->models.try_emplace(key, std::make_unique<NNModel>(path, repo->temperature, repo->seed));
			}
			catch(std::exception & e)
			{
				logAi->error("MMAI: error loading " + key + ": " + std::string(e.what()));
			}
		}

		auto fallback = json["fallback"].isNull() ? "BattleAI" : json["fallback"].String();
		logAi->debug("MMAI: preparing fallback model: %s", fallback);
		repo->fallbackModel = std::make_unique<ScriptedModel>(fallback);
		repo->fallbackName = fallback;

		return repo;
	}

	ModelRepository & GetModelRepository()
	{
		static auto repository = InitModelRepository();
		return *repository;
	}

	Schema::IModel * GetModel(const std::string & key)
	{
		auto & repository = GetModelRepository();
		auto it = repository.models.find(key);
		if(it == repository.models.end())
		{
			logAi->error("MMAI: no %s model loaded, trying fallback: %s", key, repository.fallbackName);
			ASSERT(repository.fallbackModel, "fallback failed: model is null");
			return repository.fallbackModel.get();
		}

		return it->second.get();
	}
}

Router::Router()
{
	std::ostringstream oss;
	// Store the memory address and include it in logging
	const auto * ptr = static_cast<const void *>(this);
	oss << ptr;
	addrstr = oss.str();
	info("+++ constructor +++"); // log after addrstr is set
}

Router::~Router()
{
	info("--- destructor ---");
	cb->waitTillRealize = wasWaitingForRealize;
}

void Router::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	info("*** initBattleInterface ***");
	// Diagnostic override used by the exact-equivalence harness. Normal games
	// enable this automatically only for the in-battle Q fast resolve command.
	const char * forceFastResolve = std::getenv("VCMI_MMAI_LOSSLESS_FAST_RESOLVE");
	if(forceFastResolve && strcmp(forceFastResolve, "1") == 0)
		autocombatPreferences.losslessFastResolve = true;
	env = ENV;
	cb = CB;
	colorname = cb->getPlayerID()->toString();
	wasWaitingForRealize = cb->waitTillRealize;

	cb->waitTillRealize = false;
	bai.reset();
}

void Router::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences prefs)
{
	autocombatPreferences = prefs;
	initBattleInterface(ENV, CB);
}

void Router::saveLocalState(JsonNode & dest) const
{
	JsonNode result;
	int32_t savedModels = 0;
	for(const std::string role : {"attacker", "defender"})
	{
		auto & models = GetModelRepository().models;
		const auto found = models.find(role);
		if(found == models.end())
			continue;
		auto * stochastic = dynamic_cast<NNModelStochastic *>(found->second.get());
		if(!stochastic)
			continue;

		auto & record = result["models"][role];
		record["modelName"].String() = stochastic->getName();
		record["modelVersion"].Integer() = stochastic->getVersion();
		record["modelPath"].String() = stochastic->getPath();
		record["temperature"].Float() = stochastic->getTemperature();
		record["rngState"].String() = stochastic->exportRngState();
		++savedModels;
	}

	if(savedModels == 0)
	{
		dest.clear();
		return;
	}
	result["formatVersion"].Integer() = 1;
	dest = std::move(result);
	logAi->info("MMAI: saved complete RNG state for %d stochastic model(s)", savedModels);
}

void Router::loadLocalState(const JsonNode & source)
{
	auto & repository = GetModelRepository();
	auto & models = repository.models;
	const bool supportedEnvelope = source.isStruct() && source["formatVersion"].isNumber() && source["formatVersion"].Integer() == 1;
	const uint64_t resetSeed = repository.randomSeedConfigured && !supportedEnvelope
		? CRandomGenerator::getDefault().nextInt()
		: repository.seed;
	for(const std::string role : {"attacker", "defender"})
	{
		const auto found = models.find(role);
		if(found != models.end())
			if(auto * stochastic = dynamic_cast<NNModelStochastic *>(found->second.get()))
				stochastic->resetRng(resetSeed);
	}

	if(source.isNull())
	{
		logAi->info("MMAI: no saved RNG state (new game or old save); using configured initial state");
		return;
	}
	if(!supportedEnvelope)
	{
		logAi->warn("MMAI: ignored unsupported or malformed saved RNG state");
		return;
	}

	for(const std::string role : {"attacker", "defender"})
	{
		const auto found = models.find(role);
		if(found == models.end())
			continue;
		auto * stochastic = dynamic_cast<NNModelStochastic *>(found->second.get());
		if(!stochastic)
			continue;

		const auto & record = source["models"][role];
		if(!record.isStruct())
		{
			logAi->warn("MMAI: saved RNG state has no %s stochastic model; using initial state", role);
			continue;
		}
		if(!record["modelName"].isString() || !record["modelVersion"].isNumber() || !record["modelPath"].isString()
			|| !record["temperature"].isNumber() || !record["rngState"].isString())
		{
			logAi->warn("MMAI: ignored malformed %s RNG record", role);
			continue;
		}
		if(record["modelName"].String() != stochastic->getName()
			|| record["modelVersion"].Integer() != stochastic->getVersion()
			|| record["modelPath"].String() != stochastic->getPath()
			|| record["temperature"].Float() != stochastic->getTemperature())
		{
			logAi->warn("MMAI: ignored %s RNG state because model name/version/path/temperature does not match", role);
			continue;
		}
		if(!stochastic->importRngState(record["rngState"].String()))
		{
			logAi->warn("MMAI: ignored invalid %s mt19937 state", role);
			continue;
		}
		logAi->info("MMAI: restored complete RNG state for %s model", role);
	}
}

/*
 * Delegated methods
 */

void Router::actionFinished(const BattleID & bid, const BattleAction & action)
{
	bai->actionFinished(bid, action);
}

void Router::actionStarted(const BattleID & bid, const BattleAction & action)
{
	bai->actionStarted(bid, action);
}

void Router::activeStack(const BattleID & bid, const CStack * astack)
{
	bai->activeStack(bid, astack);
}

void Router::battleAttack(const BattleID & bid, const BattleAttack * ba)
{
	bai->battleAttack(bid, ba);
}

void Router::battleCatapultAttacked(const BattleID & bid, const CatapultAttack & ca)
{
	bai->battleCatapultAttacked(bid, ca);
}

void Router::battleEnd(const BattleID & bid, const BattleResult * br, QueryID queryID)
{
	bai->battleEnd(bid, br, queryID);
}

void Router::battleGateStateChanged(const BattleID & bid, const EGateState state)
{
	bai->battleGateStateChanged(bid, state);
};

void Router::battleLogMessage(const BattleID & bid, const std::vector<MetaString> & lines)
{
	bai->battleLogMessage(bid, lines);
};

void Router::battleNewRound(const BattleID & bid)
{
	bai->battleNewRound(bid);
}

void Router::battleNewRoundFirst(const BattleID & bid)
{
	bai->battleNewRoundFirst(bid);
}

void Router::battleObstaclesChanged(const BattleID & bid, const std::vector<ObstacleChanges> & obstacles)
{
	bai->battleObstaclesChanged(bid, obstacles);
};

void Router::battleSpellCast(const BattleID & bid, const BattleSpellCast * sc)
{
	bai->battleSpellCast(bid, sc);
}

void Router::battleStackMoved(const BattleID & bid, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport)
{
	bai->battleStackMoved(bid, stack, dest, distance, teleport);
}

void Router::battleStacksAttacked(const BattleID & bid, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	bai->battleStacksAttacked(bid, bsa, ranged);
}

void Router::battleStacksEffectsSet(const BattleID & bid, const SetStackEffect & sse)
{
	bai->battleStacksEffectsSet(bid, sse);
}

void Router::battleStart(
	const BattleID & bid,
	const CCreatureSet * army1,
	const CCreatureSet * army2,
	int3 tile,
	const CGHeroInstance * hero1,
	const CGHeroInstance * hero2,
	BattleSide side,
	bool replayAllowed
)
{
	Schema::IModel * model;
	const std::string modelkey = side == BattleSide::ATTACKER ? "attacker" : "defender";
	model = GetModel(modelkey);
	if(const auto battleCallback = cb->getBattle(bid))
	{
		const auto * battleInfo = dynamic_cast<const BattleInfo *>(battleCallback->getBattle());
		if(battleInfo && battleInfo->arenaProbeSeed >= 0)
		{
			auto * stochastic = dynamic_cast<NNModelStochastic *>(model);
			if(!stochastic)
				throw std::runtime_error("ArenaBattleProbe requires a stochastic MMAI model so every scenario can reset its RNG");
			stochastic->resetRng(static_cast<uint64_t>(battleInfo->arenaProbeSeed));
			logAi->info("[ARENA-PROBE] MMAI controller seeded: battle=%d side=%s seed=%d model=%s version=%d",
				bid.getNum(), modelkey, battleInfo->arenaProbeSeed, model->getName(), model->getVersion());
		}
	}

	auto modelside = model->getSide();
	auto realside = static_cast<Schema::Side>(EI(side));

	if(modelside != realside && modelside != Schema::Side::BOTH)
		logAi->warn("The loaded '%s' model was not trained to play as %s", modelkey, modelkey);

	switch(model->getType())
	{
		case Schema::ModelType::SCRIPTED:
			if(model->getName() == "StupidAI")
			{
				bai = CDynLibHandler::getNewBattleAI("StupidAI");
				bai->initBattleInterface(env, cb, autocombatPreferences);
			}
			else if(model->getName() == "BattleAI")
			{
				bai = CDynLibHandler::getNewBattleAI("BattleAI");
				bai->initBattleInterface(env, cb, autocombatPreferences);
			}
			else
			{
				THROW_FORMAT("Unexpected scripted model name: %s", model->getName());
			}
			break;
		case Schema::ModelType::NN:
			// XXX: must not call initBattleInterface here
			bai = Base::Create(model, env, cb, autocombatPreferences.enableSpellsUsage,
				GetModelRepository().maxPredictions, autocombatPreferences.losslessFastResolve);
			break;

		default:
			THROW_FORMAT("Unexpected model type: %d", EI(model->getType()));
	}

	bai->battleStart(bid, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void Router::battleTriggerEffect(const BattleID & bid, const BattleTriggerEffect & bte)
{
	bai->battleTriggerEffect(bid, bte);
}

void Router::battleUnitsChanged(const BattleID & bid, const std::vector<UnitChanges> & changes)
{
	bai->battleUnitsChanged(bid, changes);
}

void Router::yourTacticPhase(const BattleID & bid, int distance)
{
	bai->yourTacticPhase(bid, distance);
}

/*
 * private
 */

void Router::error(const std::string & text) const
{
	log(ELogLevel::ERROR, text);
}
void Router::warn(const std::string & text) const
{
	log(ELogLevel::WARN, text);
}
void Router::info(const std::string & text) const
{
	log(ELogLevel::INFO, text);
}
void Router::debug(const std::string & text) const
{
	log(ELogLevel::DEBUG, text);
}
void Router::trace(const std::string & text) const
{
	log(ELogLevel::TRACE, text);
}
void Router::log(ELogLevel::ELogLevel level, const std::string & text) const
{
	if(logAi->getEffectiveLevel() <= level)
		logAi->debug("Router-%s [%s] %s", addrstr, colorname, text);
}
}
