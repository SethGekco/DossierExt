#include "Dossier/Observatory.h"

#include <map>

namespace
{
	std::map<int, HouseObs> g_table;
}

const char* TierName(Tier const t)
{
	switch (t)
	{
	case Tier::Winning:   return "WINNING";
	case Tier::Even:      return "EVEN";
	case Tier::Losing:    return "LOSING";
	case Tier::Desperate: return "DESPERATE";
	}
	return "?";
}

void Observatory::Reset()
{
	g_table.clear();
}

HouseObs& Observatory::Get(int const houseIndex)
{
	return g_table[houseIndex];
}

HouseObs* Observatory::Find(int const houseIndex)
{
	auto const it = g_table.find(houseIndex);
	return it != g_table.end() ? &it->second : nullptr;
}
