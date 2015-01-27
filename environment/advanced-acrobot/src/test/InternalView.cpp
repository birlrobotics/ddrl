#define NO_PARALLEL
#include "ODEFactory.hpp"

#include "AdvancedAcrobotWorld.hpp"
#include <bib/Utils.hpp>
#include <drawstuff.h>
#include "Draw.hpp"
#include <AdvancedAcrobotWorldView.hpp>

void way2() {
    AdvancedAcrobotWorldView simu("data/textures");

    for(int n=0; n<6; n++) {
        simu.resetPositions();
        for(int i=0; i<5000; i++) {
            std::vector<float> motors(simu.activated_motors(), bib::Utils::randin(-1,1));
            simu.step(motors);
        }
    }

}


int main(int, char **) {

    way2();
    ODEFactory::endInstance();

    return 0;
}
