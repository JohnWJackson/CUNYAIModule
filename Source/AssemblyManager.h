#pragma once
#include "MeatAIModule.h"
#include "InventoryManager.h"

using namespace BWAPI;
using namespace Filter;
using namespace std;

class Build_Order_Object {
private:

    UnitType _unit_in_queue;
    UpgradeType _upgrade_in_queue;

public:

    Build_Order_Object( UnitType unit ) {
        _unit_in_queue = unit;
        _upgrade_in_queue = UpgradeTypes::None;
    };

    Build_Order_Object( UpgradeType up ) {
        _unit_in_queue = UnitTypes::None;
        _upgrade_in_queue = up;
    };

    UnitType Build_Order_Object::getUnit() {
            return _unit_in_queue;
    };

    UpgradeType Build_Order_Object::getUpgrade() {
            return _upgrade_in_queue;
    };
};

struct Building_Gene {
    Building_Gene();
    Building_Gene( string s );

    vector<Build_Order_Object> building_gene_;  // how many of each of these do we want? Base build is going to be rushing mutalisk.
    string initial_building_gene_;

    bool ever_clear_ = false;
    UnitType last_build_order;

    void getInitialBuildOrder(string s);
    void updateRemainingBuildOrder( const Unit &u ); // drops item from list as complete.
    void updateRemainingBuildOrder( const UpgradeType &ups ); // drops item from list as complete.
	void updateRemainingBuildOrder(const UnitType &ut); // drops item from list as complete.
    void clearRemainingBuildOrder(); // empties the build order.
    void setBuilding_Complete( UnitType ut );  // do we have a guy going to build it?
    bool checkBuilding_Desired( UnitType ut ); 
    bool checkUpgrade_Desired( UpgradeType upgrade );
    bool checkEmptyBuildOrder();
};

