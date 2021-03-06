#ifndef DISCRETACTION_H
#define DISCRETACTION_H

#include <vector>
#include <string>

#include "arch/AAgent.hpp"
#include "sml/QLearning.hpp"
#include "sml/ActionFactory.hpp"

typedef boost::shared_ptr<std::vector<double>> EnvState;

class DiscretActionAg : public arch::AAgent<> {
 public:
  DiscretActionAg(unsigned int _nb_motors, unsigned int _nb_sensors)
    : nb_motors(_nb_motors), nb_sensors(_nb_sensors), weighted_reward(0.f), pow_gamma(1.f) {
    ainit = nullptr;
    actions = nullptr;
    algo = nullptr;
    act_templ = nullptr;
    rlparam = nullptr;
  }

  virtual ~DiscretActionAg() {
    delete ainit;
    delete actions;
    delete algo;
    delete act_templ;
    delete rlparam;
  }

  const std::vector<double>& runf(double reward, const std::vector<double>& sensors,
                                 bool learning, bool goal_reached, bool) override {

    if(reward >= 1.)
      reward = 1000;

    EnvState s(new std::vector<double>(sensors));
    sml::DAction* ac;
    if (learning)
      ac = algo->learn(s, reward, goal_reached);
    else
      ac = algo->decision(s, false);

    vector<double>* outputs = sml::ActionFactory::computeOutputs(ac, 0, *actions);
    if (!learning)
      delete ac;

    weighted_reward += reward * pow_gamma;
    pow_gamma *= rlparam->gamma;

    return *outputs;
  }

  void _unique_invoke(boost::property_tree::ptree* pt, boost::program_options::variables_map*) override {
    rlparam = new sml::RLParam;
    rlparam->epsilon             = pt->get<double>("agent.epsilon");
    rlparam->gamma               = pt->get<double>("agent.gamma");

    rlparam->alpha               = pt->get<double>("agent.alpha");
    rlparam->hidden_unit         = pt->get<int>("agent.hidden_unit");
    rlparam->activation          = pt->get<std::string>("agent.activation_function_hidden");
    rlparam->activation_stepness = pt->get<double>("agent.activation_steepness_hidden");

    rlparam->repeat_replay = pt->get<int>("agent.replay");

    int action_per_motor   = pt->get<int>("agent.action_per_motor");

    sml::ActionFactory::getInstance()->gridAction(nb_motors, action_per_motor);
    actions = new sml::list_tlaction(sml::ActionFactory::getInstance()->getActions());

    act_templ = new sml::ActionTemplate( {"effectors"}, {sml::ActionFactory::getInstance()->getActionsNumber()});
    ainit = new sml::DAction(act_templ, {0});
    algo = new sml::QLearning<EnvState>(act_templ, *rlparam, nb_sensors);
  }

  void start_episode(const std::vector<double>& sensors, bool) override {
    EnvState s(new std::vector<double>(sensors));
    algo->startEpisode(s, *ainit);
    weighted_reward = 0;
    pow_gamma = 1.d;
  }

  void end_episode() override {
    algo->resetTraces(weighted_reward);
  }

  void save(const std::string& path) override {
    algo->write(path);
  }

  void load(const std::string& path) override {
    algo->read(path);
  }

 protected:
  void _display(std::ostream& stdout) const override {
#ifndef NDEBUG
    stdout << " TTT " << algo->history_size() << " " << algo->weight_sum() << " " << algo->mse() << " " <<
           std::setprecision(5) << weighted_reward;
#endif
  }

 private:
  int nb_motors;
  int nb_sensors;
  double weighted_reward;
  double pow_gamma;

  sml::QLearning<EnvState>* algo;
  sml::ActionTemplate* act_templ;
  sml::list_tlaction* actions;
  sml::DAction* ainit;
  sml::RLParam* rlparam;
};

#endif  // DISCRETACTION_H
