#include <doctest/doctest.h>
#include "panel.hpp"

using namespace spools;

TEST_CASE("panel width snaps to whole HP within its limits") {
	CHECK(snapPanelHp(600, 15) == 40);
	CHECK(snapPanelHp(907, 15) == 60);
	CHECK(snapPanelHp(908, 15) == 61);
	CHECK(snapPanelHp(100, 15) == kPanelMinHp);
	CHECK(snapPanelHp(1e6, 15) == kPanelMaxHp);
	CHECK(clampPanelHp(-3) == kPanelMinHp);
}
