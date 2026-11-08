#include "Dossier/Opening.h"
#include "Dossier/Config.h"

#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <SideClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	// One node of the opening book. The key is the structure sequence so far
	// ("GAPOWR>GAREFN"), so a prefix is stored once however many games shared
	// it — that sharing is the merge Rex asked for, and it's free.
	struct Node
	{
		int Games = 0;
		int Humans = 0;
		long long SumFrame = 0;          // Σ frame the prefix's last structure appeared
		std::vector<int> MarkGames;      // per mark: games graded there
		std::vector<long long> MarkSum;  // per mark: Σ tempo
	};

	using Tree = std::map<std::string, Node>;   // path -> node; "*" = the whole context
	using Book = std::map<std::string, Tree>;   // context -> tree

	struct HouseOpening
	{
		bool Init = false;
		bool Counts = false;   // false = joined late (savegame): its opening is unknowable
		bool Human = false;
		std::string Context;
		int StartSpent = 0;
		int PrevMoney = 0;
		int PrevSpent = 0;
		int PrevFrame = 0;
		int LastTick = -1;
		std::vector<int> Seen;            // FactoryProducedBuildingTypes snapshot
		std::vector<std::string> Seq;     // structures in build order, capped
		std::vector<int> SeqFrames;
		size_t NextMark = 0;
		std::vector<int> Tempo;           // score at each mark reached
	};

	Book g_baseline;
	bool g_loaded = false;
	std::map<int, HouseOpening> g_houses;
	std::set<std::string> g_reported;    // contexts whose BOOK line was logged this match
	int g_saveAfter = -1;                // save on the first tick past this frame
	bool g_dirty = false;                // a mark was graded since the last save

	std::string BookPath()
	{
		return DossierConfig::Instance.ProfileDir + "\\_Openings.ini";
	}

	std::string MarksKey()
	{
		std::string key;
		for (int const m : DossierConfig::Instance.OpeningMarks)
			key += (key.empty() ? "" : ",") + std::to_string(m);
		return key;
	}

	std::string ContextOf(HouseClass* const pHouse, int const startMoney)
	{
		char const* side = "Side?";
		if (auto const pSide = SideClass::Array.GetItemOrDefault(pHouse->SideIndex))
			side = pSide->get_ID();
		char const* ctl = "Human";
		if (!pHouse->IsControlledByHuman())
		{
			// The engine's enum is inverted: 0 = Hard, 2 = Easy.
			switch (pHouse->GetAIDifficultyIndex())
			{
			case 0: ctl = "AIHard"; break;
			case 1: ctl = "AINormal"; break;
			default: ctl = "AIEasy"; break;
			}
		}
		// Start money to the nearest 1000: 10000 and 10050 are the same game.
		char buf[96];
		std::snprintf(buf, sizeof(buf), "%s.C%d.%s", side, (startMoney + 500) / 1000 * 1000, ctl);
		return buf;
	}

	void SizeMarks(Node& n)
	{
		size_t const marks = DossierConfig::Instance.OpeningMarks.size();
		n.MarkGames.resize(marks, 0);
		n.MarkSum.resize(marks, 0);
	}

	// Minimal reader for our own file: sections + key=value. Lines are longer
	// than the profile reader allows (a 16-structure path plus 2 numbers per
	// mark), hence a reader of its own.
	void Load()
	{
		g_loaded = true;
		g_baseline.clear();
		auto const path = BookPath();
		FILE* const f = std::fopen(path.c_str(), "r");
		if (!f)
		{
			Debug::Log("[DossierExt] opening book: no file yet (%s), starting fresh.\n", path.c_str());
			return;
		}

		std::string const marksKey = MarksKey();
		bool marksOk = false;
		std::string section;
		int nodes = 0;
		char line[2048];
		while (std::fgets(line, sizeof(line), f))
		{
			char* s = line;
			while (*s == ' ' || *s == '\t')
				++s;
			char* e = s + std::strlen(s);
			while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
				--e;
			*e = '\0';
			if (!*s || *s == ';')
				continue;
			if (*s == '[')
			{
				if (char* const close = std::strchr(s, ']'))
				{
					*close = '\0';
					section = s + 1;
				}
				continue;
			}
			char* const eq = std::strchr(s, '=');
			if (!eq)
				continue;
			*eq = '\0';
			std::string const key = s;
			char const* p = eq + 1;

			if (section == "Openings")
			{
				if (key == "Marks")
					marksOk = marksKey == p;
				continue;
			}
			// Tempo sums are per mark: graded against other marks they are
			// meaningless, so a changed Marks= starts a fresh book.
			if (!marksOk || section.compare(0, 8, "Opening.") != 0)
				continue;

			Node n;
			SizeMarks(n);
			char* next = nullptr;
			n.Games = static_cast<int>(std::strtol(p, &next, 10));
			p = *next == ',' ? next + 1 : next;
			n.Humans = static_cast<int>(std::strtol(p, &next, 10));
			p = *next == ',' ? next + 1 : next;
			n.SumFrame = std::strtoll(p, &next, 10);
			for (size_t m = 0; m < n.MarkGames.size() && *next == ','; ++m)
			{
				p = next + 1;
				n.MarkGames[m] = static_cast<int>(std::strtol(p, &next, 10));
				if (*next != ',')
					break;
				p = next + 1;
				n.MarkSum[m] = std::strtoll(p, &next, 10);
			}
			if (n.Games > 0)
			{
				g_baseline[section.substr(8)][key] = n;
				++nodes;
			}
		}
		std::fclose(f);

		if (!marksOk)
		{
			auto const old = path + ".old-marks";
			MoveFileExA(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
			Debug::Log("[DossierExt] opening book: Marks= changed (now %s); old book kept as %s, "
				"starting fresh.\n", marksKey.c_str(), old.c_str());
			g_baseline.clear();
			return;
		}
		Debug::Log("[DossierExt] opening book loaded: %u contexts, %d nodes.\n",
			g_baseline.size(), nodes);
	}

	// Deepest mark with enough games first, then the best mean there: a branch
	// is only preferred over its siblings on evidence they can be compared on.
	bool Grade(Node const& n, int& mark, double& mean)
	{
		int const minSamples = DossierConfig::Instance.OpeningMinSamples;
		for (int m = static_cast<int>(n.MarkGames.size()) - 1; m >= 0; --m)
			if (n.MarkGames[m] >= minSamples)
			{
				mark = m;
				mean = static_cast<double>(n.MarkSum[m]) / n.MarkGames[m];
				return true;
			}
		return false;
	}

	// The book's best line for a context, from history only (not this match).
	void ReportBook(std::string const& context)
	{
		if (!g_reported.insert(context).second)
			return;
		auto const& cfg = DossierConfig::Instance;
		auto const it = g_baseline.find(context);
		Node const* pAll = nullptr;
		if (it != g_baseline.end())
			if (auto const a = it->second.find("*"); a != it->second.end())
				pAll = &a->second;
		if (!pAll)
		{
			Debug::Log("[DossierExt] BOOK %s: no games yet.\n", context.c_str());
			return;
		}
		auto const& tree = it->second;
		auto const& all = *pAll;

		std::string path, line;
		int lastMark = -1;
		double lastMean = 0;
		for (;;)
		{
			std::string const prefix = path.empty() ? "" : path + ">";
			auto best = tree.end();
			int bestMark = -1;
			double bestMean = 0;
			for (auto c = path.empty() ? tree.begin() : tree.lower_bound(prefix); c != tree.end(); ++c)
			{
				if (!path.empty() && c->first.compare(0, prefix.size(), prefix) != 0)
					break;
				if (c->first == "*" || c->first.find('>', prefix.size()) != std::string::npos)
					continue;   // the context node, or not a direct child
				int mark;
				double mean;
				if (Grade(c->second, mark, mean)
					&& (mark > bestMark || (mark == bestMark && mean > bestMean)))
				{
					best = c;
					bestMark = mark;
					bestMean = mean;
				}
			}
			if (best == tree.end())
				break;
			path = best->first;
			lastMark = bestMark;
			lastMean = bestMean;
			char step[96];
			std::snprintf(step, sizeof(step), "%s%s@f%lld", line.empty() ? "" : " > ",
				path.c_str() + prefix.size(), best->second.SumFrame / best->second.Games);
			line += step;
		}

		if (line.empty())
		{
			Debug::Log("[DossierExt] BOOK %s: %d games (%d human), no line with %d+ games yet.\n",
				context.c_str(), all.Games, all.Humans, cfg.OpeningMinSamples);
			return;
		}
		double const ctxMean = lastMark < static_cast<int>(all.MarkGames.size()) && all.MarkGames[lastMark]
			? static_cast<double>(all.MarkSum[lastMark]) / all.MarkGames[lastMark] : 0;
		Debug::Log("[DossierExt] BOOK %s: %d games (%d human). Best line: %s | tempo@f%d %.0f "
			"vs %.0f for the context.\n",
			context.c_str(), all.Games, all.Humans, line.c_str(),
			cfg.OpeningMarks[lastMark], lastMean, ctxMean);
	}

	// Fold this match's openings into a copy of the baseline. A mark only counts
	// for nodes whose structure existed by then, so a branch is graded on what
	// happened AFTER that choice, not before it.
	void AddMatch(Book& book)
	{
		auto const& marks = DossierConfig::Instance.OpeningMarks;
		for (auto const& entry : g_houses)
		{
			auto const& h = entry.second;
			// A house that built nothing was idle (an unattended test slot, a
			// player only watching), not playing an opening: grading it would
			// drag every context's baseline toward zero.
			if (!h.Counts || h.Tempo.empty() || h.Seq.empty())
				continue;
			auto& tree = book[h.Context];
			auto add = [&](std::string const& key, int const frameAt)
			{
				auto& n = tree[key];
				SizeMarks(n);
				++n.Games;
				if (h.Human)
					++n.Humans;
				n.SumFrame += frameAt;
				for (size_t m = 0; m < h.Tempo.size() && m < marks.size(); ++m)
					if (marks[m] >= frameAt)
					{
						++n.MarkGames[m];
						n.MarkSum[m] += h.Tempo[m];
					}
			};
			add("*", 0);
			std::string path;
			for (size_t k = 0; k < h.Seq.size(); ++k)
			{
				if (k)
					path += '>';
				path += h.Seq[k];
				add(path, h.SeqFrames[k]);
			}
		}
	}

	// Over the cap, drop the least-evidenced leaf (deepest first on ties).
	// Removing only leaves keeps every stored path's prefixes intact.
	void Prune(Tree& tree, int const maxNodes)
	{
		if (maxNodes <= 0)
			return;
		while (static_cast<int>(tree.size()) > maxNodes)
		{
			auto victim = tree.end();
			for (auto it = tree.begin(); it != tree.end(); ++it)
			{
				if (it->first == "*")
					continue;
				std::string const childPrefix = it->first + ">";
				auto const next = tree.lower_bound(childPrefix);
				if (next != tree.end() && next->first.compare(0, childPrefix.size(), childPrefix) == 0)
					continue;   // has an extension: not a leaf
				if (victim == tree.end() || it->second.Games < victim->second.Games
					|| (it->second.Games == victim->second.Games && it->first.size() > victim->first.size()))
					victim = it;
			}
			if (victim == tree.end())
				break;
			tree.erase(victim);
		}
	}
}

void Opening::Reset()
{
	g_baseline.clear();
	g_loaded = false;
	g_houses.clear();
	g_reported.clear();
	g_saveAfter = -1;
	g_dirty = false;
}

void Opening::Tick(HouseClass* const pHouse)
{
	auto const& cfg = DossierConfig::Instance;
	// Civilian/Special houses never build an opening.
	if (!cfg.OpeningsEnabled || cfg.OpeningMarks.empty() || !pHouse
		|| (pHouse->Type && pHouse->Type->MultiplayPassive))
		return;
	int const frame = Unsorted::CurrentFrame;

	// Deferred save: by now every house has had its tick at the last mark.
	if (g_saveAfter >= 0 && frame > g_saveAfter)
	{
		g_saveAfter = -1;
		Save("mark");
	}
	if (frame > cfg.OpeningMarks.back() + cfg.OpeningTick)
		return;

	auto& h = g_houses[pHouse->ArrayIndex];
	if (!h.Init)
	{
		h.Init = true;
		h.Counts = frame <= cfg.OpeningTick * 4;
		if (!h.Counts)
			return;
		if (!g_loaded)
			Load();
		int const money = pHouse->Available_Money();
		h.Human = pHouse->IsControlledByHuman();
		h.Context = ContextOf(pHouse, money);
		h.StartSpent = h.PrevSpent = pHouse->CreditsSpent;
		h.PrevMoney = money;
		h.PrevFrame = frame;
		ReportBook(h.Context);
	}
	if (!h.Counts || (h.LastTick >= 0 && frame - h.LastTick < cfg.OpeningTick))
		return;
	bool const first = h.LastTick < 0;
	h.LastTick = frame;

	// The const overload: the non-const GetItemCount grows the ENGINE's vector.
	CounterClass const& built = pHouse->FactoryProducedBuildingTypes;
	int const types = BuildingTypeClass::Array.Count;
	if (static_cast<int>(h.Seen.size()) < types)
		h.Seen.resize(types, 0);
	for (int i = 0; i < types; ++i)
	{
		int const now = built.GetItemCount(i);
		int const was = h.Seen[i];
		if (now == was)
			continue;
		h.Seen[i] = now;
		// First pass is the baseline; the counter never decreases, so a drop
		// would mean a reset we don't understand — record nothing from it.
		if (first || now < was)
			continue;
		auto const pType = BuildingTypeClass::Array.GetItem(i);
		if (!pType || pType->Wall)
			continue;   // wall drags are dozens of entries that say nothing about an opening
		for (int k = was; k < now && static_cast<int>(h.Seq.size()) < cfg.OpeningDepth; ++k)
		{
			h.Seq.push_back(pType->get_ID());
			h.SeqFrames.push_back(frame);
		}
	}

	while (h.NextMark < cfg.OpeningMarks.size() && frame >= cfg.OpeningMarks[h.NextMark])
	{
		int const mark = cfg.OpeningMarks[h.NextMark];
		int const money = pHouse->Available_Money();
		int const spent = pHouse->CreditsSpent;
		int const dt = std::max(1, frame - h.PrevFrame);
		// Money in = float change + spend (HarvestedCredits reads constant in
		// this stack — the same derivation Economy.cpp uses).
		int const income = (money - h.PrevMoney) + (spent - h.PrevSpent);
		double const rate = static_cast<double>(income) / dt;
		int const spentSoFar = spent - h.StartSpent;
		bool const lowPower = pHouse->PowerDrain > 0 && pHouse->PowerOutput < pHouse->PowerDrain;
		int const tempo = spentSoFar + static_cast<int>(rate * cfg.OpeningHorizon)
			- (lowPower ? cfg.OpeningLowPowerPenalty : 0);
		h.Tempo.push_back(tempo);
		h.PrevMoney = money;
		h.PrevSpent = spent;
		h.PrevFrame = frame;
		++h.NextMark;
		g_dirty = true;
		g_saveAfter = std::max(g_saveAfter, mark + cfg.OpeningTick);

		if (cfg.OpeningLog)
		{
			std::string line;
			for (size_t k = 0; k < h.Seq.size(); ++k)
			{
				char step[64];
				std::snprintf(step, sizeof(step), "%s%s@f%d", k ? " > " : "",
					h.Seq[k].c_str(), h.SeqFrames[k]);
				line += step;
			}
			Debug::Log("[DossierExt] opening %s#%d [%s] f%d: tempo %d = spent %d + income %.2f/f x%d%s"
				" | money %d | %s\n",
				pHouse->get_ID(), pHouse->ArrayIndex, h.Context.c_str(), mark, tempo, spentSoFar,
				rate, cfg.OpeningHorizon, lowPower ? " - LOW POWER" : "", money,
				line.empty() ? "(nothing built: idle, not graded)" : line.c_str());
		}
	}
}

void Opening::Save(const char* const why)
{
	auto const& cfg = DossierConfig::Instance;
	if (!cfg.OpeningsEnabled || !g_loaded || !g_dirty)
		return;
	g_dirty = false;

	Book book = g_baseline;
	AddMatch(book);
	int nodes = 0;
	for (auto& [context, tree] : book)
	{
		Prune(tree, cfg.OpeningMaxNodes);
		nodes += static_cast<int>(tree.size());
	}

	CreateDirectoryA(cfg.ProfileDir.c_str(), nullptr); // no-op if it already exists
	auto const path = BookPath();
	auto const tmp = path + ".tmp";
	FILE* const f = std::fopen(tmp.c_str(), "w");
	if (!f)
	{
		Debug::Log("[DossierExt] WARNING: cannot write opening book %s\n", tmp.c_str());
		return;
	}
	std::fprintf(f, "; DossierExt opening book — generated. Every house, AI or human, adds its\n"
		"; first minutes; openings that share a start share the stored nodes.\n"
		"; path=games,human games,sum of frames the last structure appeared,\n"
		";      then per mark: games graded there, sum of tempo\n");
	std::fprintf(f, "[Openings]\nVersion=1\nMarks=%s\n", MarksKey().c_str());
	for (auto const& [context, tree] : book)
	{
		std::fprintf(f, "\n[Opening.%s]\n", context.c_str());
		for (auto const& [key, n] : tree)
		{
			std::fprintf(f, "%s=%d,%d,%lld", key.c_str(), n.Games, n.Humans, n.SumFrame);
			for (size_t m = 0; m < n.MarkGames.size(); ++m)
				std::fprintf(f, ",%d,%lld", n.MarkGames[m], n.MarkSum[m]);
			std::fprintf(f, "\n");
		}
	}
	std::fclose(f);
	// Rename over the old file so a crash mid-write can't leave half a book.
	if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		Debug::Log("[DossierExt] WARNING: cannot replace opening book %s (error %lu)\n",
			path.c_str(), GetLastError());
		return;
	}

	int graded = 0;
	for (auto const& [idx, h] : g_houses)
		if (h.Counts && !h.Tempo.empty() && !h.Seq.empty())
			++graded;
	Debug::Log("[DossierExt] opening book saved (%s, f%d): %u contexts, %d nodes, %d houses graded "
		"this match.\n", why, Unsorted::CurrentFrame, book.size(), nodes, graded);
}
