/*
 * BonusParameters.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <utility>
#include <variant>

#include "../serializer/Serializeable.h"

#include "Bonus.h"

VCMI_LIB_NAMESPACE_BEGIN

struct BonusParametersOnCombatEvent
{
	struct CombatEffectBonus
	{
		std::shared_ptr<const Bonus> bonus;
		bool targetEnemy = false;

		template <class H>
		void serialize(H& h)
		{
			h & bonus;
			h & targetEnemy;
		}
	};

	struct CombatEffectSpell
	{
		SpellID spell;
		int masteryLevel = 0;
		bool targetEnemy = false;

		template <class H>
		void serialize(H& h)
		{
			h & spell;
			h & masteryLevel;
			h & targetEnemy;
		}
	};

	using CombatEffect = std::variant<CombatEffectBonus, CombatEffectSpell>;

	std::vector<CombatEffect> effects;

	template <class H>
	void serialize(H& h)
	{
		h & effects;
	}
};

class DLL_LINKAGE BonusParameters final : public Serializeable
{
public:
	using storage_type = std::variant<int32_t, CreatureID, SpellID, std::vector<int32_t>, BonusParametersOnCombatEvent>;

	BonusParameters() = default;

	template<typename DataType>
	BonusParameters(const DataType & value)
		:data_(value)
	{}

	std::string toString() const;
	JsonNode toJsonNode() const;

	/// Typed read-only access for evidence exporters. The configuration JSON
	/// representation is not a lossless representation of every parameter kind.
	// A member template is instantiated by the consumer, including DLL clients.
	template<typename Visitor>
	decltype(auto) visit(Visitor && visitor) const
	{
		return std::visit(std::forward<Visitor>(visitor), data_);
	}

	CreatureID toCreature() const
	{
		return toCustom<CreatureID>();
	}

	SpellID toSpell() const
	{
		return toCustom<SpellID>();
	}

	int32_t toNumber() const
	{
		return toCustom<int32_t>();
	}

	const std::vector<int32_t> & toVector() const
	{
		return toCustom<std::vector<int32_t>>();
	}

	template<typename CustomType>
	const CustomType & toCustom() const
	{
		auto * result = std::get_if<CustomType>(&data_);
		if (result)
			return *result;
		throw std::runtime_error("Invalid addInfo type access! Stored type: " + std::to_string(data_.index()));
	}

	template<typename CustomType>
	CustomType & toCustom()
	{
		auto * result = std::get_if<CustomType>(&data_);
		if (result)
			return *result;
		throw std::runtime_error("Invalid addInfo type access! Stored type: " + std::to_string(data_.index()));
	}

	template <class H>
	void serialize(H& h)
	{
		h & data_;
	}

private:
	storage_type data_;
};

VCMI_LIB_NAMESPACE_END
