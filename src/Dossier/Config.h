#pragma once

#include <set>
#include <string>

// Parsed [Dossier.*] config + the per-difficulty DossierExt.* keys from the
// vanilla rules difficulty sections ([Easy]/[Normal]/[Difficult]).
//
// Difficulty mapping (DESIGN.md §5a): the engine's multiplier sections are
// INVERTED for AI houses (AIDifficulty 0=Hard reads [Easy]). Our DossierExt.*
// keys are OURS, not engine multipliers, so they use the INTUITIVE mapping:
// a Hard AI reads [Difficult]. Phase 0 logs both resolutions per house so the
// mapping is verified in-game, not assumed.
struct DossierDifficultyKeys
{
	bool Shrouded = false;   // Observatory may read shrouded/gapped cells
	bool Escalation = false; // Scoreboard may unlock dirty tiers
	bool AnySet = false;     // any DossierExt.* key present in the section
};

struct DossierConfig
{
	bool Parsed = false;

	// [Dossier.General]
	bool Enabled = true;
	bool DebugTicks = false;
	int CheckpointInterval = 3000;          // frames between profile flushes
	std::string ProfileDir = "DossierProfiles";

	// Phase 1 — Observatory
	int EconWindow = 450;                    // frames per economy sample
	double EconSmoothing = 0.3;              // EMA alpha for income (bursty unloads)
	int SurveyPeriod = 900;                  // frames between map/army scans
	int OreReachRadius = 30;                 // cells: "reachable ore" near base
	int ScoreboardPeriod = 450;              // frames between standing re-evals

	// Phase 2 — Dossier distillation
	int OpeningMaxEvents = 40;               // build-order events kept for the opening
	double RecencyWeight = 0.4;              // EMA weight of THIS game vs history

	// Phase 2.1 — [Dossier.Identity]: names are costumes, not identities.
	// Blend = consult the install-wide human record until a NAME earns its own
	// standing (enough games AND a measurably different habit vector).
	std::string IdentityMode = "Blend";      // Blend | NameOnly | GlobalOnly
	std::string GlobalProfileName = "_AllHumans";
	int TrustNameAfter = 3;                  // games before a name can stand alone
	double DivergenceThreshold = 0.35;       // habit distance = "distinct persona"
	bool MPNameTrust = true;                 // MP lobby names are real people

	// MP desync policy (DESIGN §7). Every client simulates every AI, so if a
	// LOCAL profile file ever influenced the sim the clients would diverge
	// instantly. Therefore with 2+ humans: keep RECORDING (writing a file has
	// no sim effect, and every client independently observes the same game, so
	// everyone's install learns) but never let profile data reach a decision.
	// Observatory + Scoreboard read synced sim state and stay fully live.
	bool RecordInMultiplayer = true;         // learn from MP games
	bool ActOnProfilesInMultiplayer = false; // NEVER default-on: desync risk

	// Phase 2.1 — [Dossier.Records]: every layer on by default, each toggleable
	// (modders watching profile file size can switch layers off).
	bool RecOverall = true;                  // cross-country "this human" layer
	bool RecPerCountry = true;
	bool RecPerMap = true;                   // per-map records
	bool RecPerSpawn = true;                 // split map records by spawn point
	bool RecSpatial = true;                  // attack / rush / build heatmaps
	int SpatialBucket = 8;                   // cells per heatmap bucket
	int SpatialTopN = 12;                    // hottest buckets kept per grid
	int RushWindow = 9000;                   // frames: "early" attacks = a rush

	// Phase 2.2 — does a strategy TRANSFER across maps/countries?
	bool TransferReport = true;
	// A single game always looks "specific" — its own noise IS the whole
	// sample. Require real evidence before issuing a verdict.
	int TransferMinGames = 2;
	double TransferThreshold = 0.30;         // below = portable habit
	double MapSimilarThreshold = 0.25;       // below = maps play alike

	// Phase 1 — [Dossier.Scoreboard] (tier thresholds; DESIGN §5c)
	double LosingBelow = 0.8;                // standing ratio -> LOSING
	double DesperateBelow = 0.5;             // -> DESPERATE
	double WinningAbove = 1.3;               // -> WINNING
	double Hysteresis = 0.1;                 // band to climb back out of a tier
	double StandingSmoothing = 0.3;          // EMA alpha: momentum vs responsiveness
	// Standing formula weights (open question #3: start equal thirds)
	double ArmyWeight = 1.0;
	double EconWeight = 1.0;
	double TerritoryWeight = 1.0;
	// Worth of one captured tech building as "territory". Some maps (Powder
	// Keg, verified in-game) have almost no reachable ore and run entirely on
	// derricks — ore-only territory reads 0 for everyone there and the
	// dimension goes dead. Counting tech buildings keeps it meaningful.
	int TechBuildingValue = 2000;

	// What counts as a "tech building" is MAP DESIGN, not a technicality
	// (Rex, 2026-09-23): Westwood maps only ever offer civilian structures,
	// but fan maps happily place capturable ConYards and the like. So three
	// signals are accepted, any one of which qualifies — see Survey.cpp.
	// Declared list = rulesmd [AI] NeutralTechBuildings, plus our additions
	// (the stock list is complete except CASLAB).
	std::set<std::string> NeutralTechBuildings;
	std::string ExtraTechBuildings = "CASLAB";
	bool CapturedCountsAsTech = true;   // anything TAKEN counts, whatever it is
	// TechLevel<0 && Capturable looked like a neat "civilian" test but it
	// FALSE-POSITIVES on the Construction Yard (GACNST is TechLevel=-1
	// Capturable=true, because ConYards deploy rather than build) — verified
	// in-game, it silently counted every player's own ConYard. The declared
	// list is authoritative; this stays available for mods that don't declare
	// one, but defaults OFF.
	bool CivilianHeuristic = false;

	// DossierExt.* keys, indexed by AIDifficulty (0=Hard, 1=Normal, 2=Easy —
	// the engine's inverted enum; resolution helpers hide the confusion).
	DossierDifficultyKeys Diff[3];

	static DossierConfig Instance;

	static void Reset();
	static void EnsureParsed();

	// The INTUITIVE rules section this AIDifficulty reads DossierExt.* from.
	static const char* IntuitiveSection(int aiDifficulty);
	// The engine's (inverted) multiplier section for the same value, for the
	// Phase 0 verification log.
	static const char* EngineMultiplierSection(int aiDifficulty);
};
