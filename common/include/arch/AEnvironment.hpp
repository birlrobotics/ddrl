#ifndef AENVIRONMENT_H
#define AENVIRONMENT_H

#include "arch/Dummy.hpp"
#include "arch/CommonAE.hpp"

namespace arch {

template <typename ProgOptions = EnvProgOptions>
class AEnvironment : public ProgOptions, public CommonAE {
 public:
  virtual void unique_destroy() {}
  virtual const std::vector<float> &perceptions() const = 0;

  virtual unsigned int number_of_actuators() const = 0;

  virtual unsigned int number_of_sensors() const = 0;

  virtual float performance() = 0;

  virtual ~AEnvironment() { // pure?
  };

  void unique_invoke(boost::property_tree::ptree *properties,
                     boost::program_options::variables_map *vm) {
    instance_per_episode =
      properties->get<unsigned int>("environment.instance_per_episode");
    max_step_per_instance =
      properties->get<unsigned int>("environment.max_step_per_instance");
    _unique_invoke(properties, vm);
  }

  void reset_episode() {
    current_instance = 0;
    current_step = 0;
    _reset_episode();
  }

  void next_instance() {
    current_step = 0;
    current_instance++;
    _next_instance();
  }

  void apply(const std::vector<float> &actuators) {
    current_step++;
    _apply(actuators);
  }

  bool running() const {
    return current_step < max_step_per_instance && _running();
  }
  bool hasInstance() const {
    return current_instance < instance_per_episode;
  }
 protected:
  virtual void _unique_invoke(boost::property_tree::ptree *,
                              boost::program_options::variables_map *) {}

  virtual void _reset_episode() {}

  virtual void _next_instance() {}

  virtual bool _running() const {
    return true;
  }
  virtual void _apply(const std::vector<float> &) = 0;

  unsigned int current_step = 0;
  unsigned int current_instance = 0;

  unsigned int max_step_per_instance;
  unsigned int instance_per_episode;
};
}

#endif  // AENVIRONMENT_H
