#pragma once

#include <BWAPI.h>
#include "CUNYAIModule.h"

struct ScoutingManager {

    int last_overlord_scout_sent_;
    int last_zergling_scout_sent_;

    bool initial_scouts_;
    bool let_overlords_scout_;  // False against Terran of if enemy can attack overlords
    bool exists_overlord_scout_;
    bool exists_zergling_scout_;
    bool exists_expo_zergling_scout_;
    bool found_enemy_base_;
    bool force_zergling_;

    Unit overlord_scout_;
    Unit zergling_scout_;
    Unit expo_zergling_scout_;
    Unit last_overlord_scout_;
    Unit last_zergling_scout_;
    Unit last_expo_scout_;

    vector<Position> scout_start_positions_;

    unordered_map<int, Position> scout_expo_map_;
    
    // Initalizer
    ScoutingManager();

    // Checks if unit is our scouting unit
    Position getScoutTargets(const Unit &unit, Map_Inventory &inv, Unit_Inventory &ei);
    void updateScouts(const Player_Model& enemy_player_model, const Player_Model& friendly_player_model);
    bool needScout(const Unit &unit, const int &t_game) const;
    void setScout(const Unit &unit, const int &ling_type=0);
    void clearScout(const Unit &unit);
    bool isScoutingUnit(const Unit &unit) const;
    void sendScout(const Unit &unit, const Position &scout_spot) const;
    void diagnosticLine(const Unit &unit, const Map_Inventory &inv) const;
};