#include "Dossier/Observatory.h"
#include "Dossier/Config.h"

#include <cstdio>
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

std::string Observatory::BucketKey(int const cellX, int const cellY)
{
	int b = DossierConfig::Instance.SpatialBucket;
	if (b < 1)
		b = 1;
	char buf[24];
	std::snprintf(buf, sizeof(buf), "%d,%d", cellX / b, cellY / b);
	return buf;
}
