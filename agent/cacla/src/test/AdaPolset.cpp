#include "bib/Assert.hpp"
#include "arch/ACSimulator.hpp"
#include "arch/Example.hpp"
#include "AdvancedAcrobotEnv.hpp"
#include "OffPolSetACFitted.hpp"

int main(int argc, char **argv) {
  arch::ACSimulator<AdvancedAcrobotEnv, OffPolSetACFitted> s;
  
  s.init(argc, argv);
  s.enable_analyse_distance_bestVF();

  s.run();

  LOG_DEBUG("works !");
}
