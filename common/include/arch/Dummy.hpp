#ifndef DUMMY_H
#define DUMMY_H

#include <string>
#include "boost/program_options.hpp"

#include "bib/Logger.hpp"

namespace arch {

class DummyProgOptions {
 public:
  static boost::program_options::options_description program_options() {
    boost::program_options::options_description desc;
    return desc;
  }
};

class AgentProgOptions {
 public:
  static boost::program_options::options_description program_options() {
    boost::program_options::options_description desc("Allowed Agent options");
    desc.add_options()("load", boost::program_options::value<std::string>(),
                       "set the agent to load");
    return desc;
  }
};

class EnvProgOptions {
 public:
  static boost::program_options::options_description program_options() {
    boost::program_options::options_description desc(
      "Allowed environment options");
    desc.add_options()("view", "display the environment [default : false]");
    return desc;
  }
};


class DummyEpisodeStat {
 public:
  virtual void dump(uint episode, const std::vector<double>& perceptions, const std::vector<double>& motors, double reward) {
    (void) episode;
    (void) perceptions;
    (void) motors;
    (void) reward;
  }
};

class MotorEpisodeStat : public DummyEpisodeStat {
 public:

  MotorEpisodeStat() : step(0) { }

  virtual void dump(uint episode, const std::vector<double>& perceptions, const std::vector<double>& motors, double reward) {
    (void) episode;
    (void) perceptions;
    (void) reward;

    std::string sep = std::to_string(episode);;
    LOG_FILE_NNL("motors.data." + sep, step << " ");
    for(double m : motors)
      LOG_FILE_NNL("motors.data." + sep, m << " ");
    LOG_FILE("motors.data." + sep, "");

    step++;
  }

 private :
  uint step;
};

class PerceptionEpisodeStat : public DummyEpisodeStat {
 public:

  PerceptionEpisodeStat() : step(0) { }

  virtual void dump(uint episode, const std::vector<double>& perceptions, const std::vector<double>& motors, double reward) {
    (void) episode;
    (void) motors;
    (void) reward;

    std::string sep = std::to_string(episode);;
    LOG_FILE_NNL("perceptions.data." + sep, step << " ");
    for(double m : perceptions)
      LOG_FILE_NNL("perceptions.data." + sep, m << " ");
    LOG_FILE("perceptions.data." + sep, "");

    step++;
  }

 private :
  uint step;
};

}  // namespace arch

#endif  // DUMMY_H
