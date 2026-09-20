# Source scope and licensing

This repository publishes the current remote-client code as a separate history.
It includes shared serialization/game logic, networking, UI, Android/Qt launcher,
BattleAI, MMAI, EmptyAI server-decision adapter, and public combat-model resources.
Private server deployment decisions, unit matchup scoring weights and training
tools are excluded. Shared protocol definitions are intentionally public.

VCMI is GNU GPL v2 or later (see `license.txt` and source headers). Preserve all
upstream notices. Vendored FuzzyLite, innoextract and dependency recipes retain
their notices/licenses. MMAI model metadata declares Apache-2.0; upstream model
download provenance is retained in `Mods/mmai/models/sources.json`. Core VCMI
resources retain their upstream notices; this repository does not claim ownership
of third-party assets or grant rights to the Heroes III original game.

HotA and language asset bundles from local test APKs are not published here.
No production APK or executable is distributed by this initial source commit.
Correspondence to a specific distributed APK, third-party asset permissions and
the complete Android packaging/rebuild process still require verification before
an app-store release. Publishing this repository alone is not a claim that all
distribution obligations or app-store requirements have been satisfied.

Modified files are identifiable relative to the upstream base recorded in
SOURCE_PROVENANCE.json. The source snapshot is published on 2026-09-20.
