#include "bib/Assert.hpp"
#include "arch/Simulator.hpp"
#include "arch/Example.hpp"
#include "CartpoleEnv.hpp"
#include "OfflineCaclaAg.hpp"
// #include "OfflineCaclaAgIS.hpp"

class HardCoddedController : public arch::AAgent<> {
 public:
  HardCoddedController(unsigned int nb_motors, unsigned int) : actuator(nb_motors) {}
  const std::vector<double>& run(double, const std::vector<double>& s, bool, bool) override {
    
//     actuator[0] = s[2] < 0 ? -1 : 1 ;
    actuator[0] = s[2] < 0 ? -1./5. : 1./5. ;
    return actuator;
  }

  virtual ~HardCoddedController() {
  }

  std::vector<double> actuator;
};

int main(int argc, char **argv) {
  FLAGS_minloglevel = 2;
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  
  arch::Simulator<CartpoleEnv, OfflineCaclaAg<>> s;
//   arch::Simulator<CartpoleEnv, HardCoddedController> s;
//   arch::Simulator<CartpoleEnv, arch::ExampleAgent> s;
  
  s.init(argc, argv);

  s.run();

  LOG_DEBUG("works !");
}
