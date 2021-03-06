#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include <string>
#include <vector>
#include <list>
#include <type_traits>
#include "boost/property_tree/ptree.hpp"
#include "boost/property_tree/ini_parser.hpp"
#include "boost/program_options.hpp"
#include <boost/filesystem.hpp>

#include "bib/Assert.hpp"
#include "bib/Logger.hpp"
#include "bib/Utils.hpp"
#include "bib/Chrono.hpp"
#include "bib/Dumper.hpp"
#include "bib/XMLEngine.hpp"
#include "arch/AAgent.hpp"
#include "arch/AEnvironment.hpp"
#include "arch/DefaultParam.hpp"

using std::string;

namespace arch {

template <typename Environment, typename Agent, typename Stat=DummyEpisodeStat>
class Simulator {
  //     static_assert(std::is_base_of<AEnvironment<>, Environment>::value,
  //     "Environment should be a base of AEnvironment.");
  //     static_assert(std::is_base_of<AAgent<>, Agent>::value, "Agent should be
  //     a base of AAgent.");

 public:
  Simulator(uint _config_file_index=0) : max_episode(0), test_episode_per_episode(0), test_episode_at_end(0),
    dump_log_each(0), display_log_each(0), save_agent_each(0), config_file_index(_config_file_index), properties(nullptr),
    command_args(nullptr), time_spend(), env(nullptr), agent(nullptr) {}

  virtual ~Simulator() {
    delete properties;
    delete command_args;

    if (agent != nullptr)
      delete agent;
  }

  void init() {
    int argc = 0;
    char nul[] = {};
    char* argv[] = { &nul[0], NULL };
    init(argc, argv);
  }

  void init(int argc, char** argv) {
    string config_file = DEFAULT_CONFIG_FILE;
    if(config_file_index != 0)
      config_file = DEFAULT_CONFIG_FILE_BEGIN + std::to_string(config_file_index) + DEFAULT_CONFIG_FILE_END;
    readCommandArgs(argc, argv, &config_file);
    readConfig(config_file);
  }

  template <typename OAgent=Agent>
  uint before_run(uint* starting_ep, OAgent* early_stage=nullptr) {
    ASSERT(well_init, "Please call init() first on Simulator");

    env = new Environment;
    env->unique_invoke(properties, command_args);

    agent = new Agent(env->number_of_actuators(), env->number_of_sensors());
    if(early_stage != nullptr) {
      agent->provide_early_development(early_stage);
    }
    agent->unique_invoke(properties, command_args, pass_this_sim);
    
    uint fepisode = max_episode + *starting_ep;
    if(can_continue && boost::filesystem::exists("continue.simu.data")){
      agent->load_previous_run();
      auto p = bib::XMLEngine::load<simu_state>("episode","continue.simu.data");
      *starting_ep = p->episode;
      fepisode = max_episode;
      delete p;
      LOG_INFO("loading previous data ... " << *starting_ep);
      must_load_previous_run = true;
    }

    if(pass_this_sim){
      LOG_INFO("WARNING: Ignore simulator " << config_file_index);
      fepisode=0;
    }
    
    return fepisode;
  }

  void run_loop(uint starting_ep, int fepisode){
    Stat stat;

    time_spend.start();
    for (int episode = starting_ep; episode < fepisode; episode++) {
      //  learning
      run_episode(true, episode, 0, stat);

      for (int test_episode = 0; test_episode < test_episode_per_episode; test_episode++) {
        //  testing during learning
        run_episode(false, episode, test_episode, stat);
      }
      
      if(test_episode_per_episode < -1 && episode % (-test_episode_per_episode) == 0) {
        //  testing during learning
        run_episode(false, episode, 0, stat);
      }
    }

    for (unsigned int test_episode = 0; test_episode < test_episode_at_end; test_episode++) {
      //  testing after learning
      run_episode(false, max_episode + starting_ep, test_episode, stat);
    }
  }

  template <typename OAgent=Agent>
  void run(OAgent* early_stage=nullptr, uint starting_ep = 0) {
    uint fepisode = before_run(&starting_ep, early_stage);
    run_loop(starting_ep, fepisode);

    env->unique_destroy();
    delete env;

    LOG_FILE(DEFAULT_END_FILE, "" << (double)(time_spend.finish() / 60.f));  // in minutes
  }

  Agent* getAgent() {
    return agent;
  }

  uint getMaxEpisode() {
    return max_episode;
  }

 protected:
  virtual void run_episode(bool learning, unsigned int lepisode, int tepisode, Stat& stat) {
    env->reset_episode(learning);
    std::list<double> all_rewards;
    agent->start_instance(learning);

    uint instance = 0;
    while (env->hasInstance()) {
      uint step = 0;
      std::vector<double> perceptions = env->perceptions();
      agent->start_episode(perceptions, learning);

      while (env->running()) {
        perceptions = env->perceptions();
        double reward = env->performance();
        const std::vector<double>& actuators = agent->runf(reward, perceptions, learning, false, false);
        env->apply(actuators);
        stat.dump(lepisode, perceptions, actuators, reward);
        all_rewards.push_back(reward);
        step++;
      }

      // if the environment is in a final state
      //        i.e it didn't reach the number of step but finished in an absorbing state
      // then we call the algorithm a last time to give him this information
      perceptions = env->perceptions();
      double reward = env->performance();
      agent->runf(reward, perceptions, learning, env->final_state(), true);
      all_rewards.push_back(reward);

      env->next_instance(learning);
      agent->end_episode(learning);

      if(must_load_previous_run){
        std::string source_path = learning ? std::to_string(instance) + DEFAULT_DUMP_LEARNING_FILE :
        std::to_string(instance) + "." +std::to_string(tepisode) + DEFAULT_DUMP_TESTING_FILE;
        std::string destination_path = learning ? std::to_string(instance) + DEFAULT_DUMP_LEARNING_FILE :
        std::to_string(instance) + "." +std::to_string(tepisode) + DEFAULT_DUMP_TESTING_FILE;
        destination_path = "continue." + destination_path;
        boost::filesystem::copy_file(destination_path, source_path, boost::filesystem::copy_option::overwrite_if_exists);
        LOG_FILEA(source_path);
      }
      
      dump_and_display(lepisode, instance, tepisode, all_rewards, env, agent, learning, step);
      
      if(can_continue && lepisode % continue_save_each == 0 && lepisode > 0 && !must_load_previous_run) {
        std::string source_path = learning ? std::to_string(instance) + DEFAULT_DUMP_LEARNING_FILE :
                                  std::to_string(instance) + "." +std::to_string(tepisode) + DEFAULT_DUMP_TESTING_FILE;
        std::string destination_path = learning ? std::to_string(instance) + DEFAULT_DUMP_LEARNING_FILE :
                                        std::to_string(instance) + "." +std::to_string(tepisode) + DEFAULT_DUMP_TESTING_FILE;
        destination_path = "continue." + destination_path;
        boost::filesystem::copy_file(source_path, destination_path, boost::filesystem::copy_option::overwrite_if_exists);
      }
      instance++;
    }

    agent->end_instance(learning);
    
    if(can_continue && learning){
      if(lepisode % continue_save_each == 0 && !must_load_previous_run && lepisode > 0){
        agent->save_run();
        bib::XMLEngine::save<simu_state>({lepisode+1}, "episode", "continue.simu.data");
      }
      if(test_episode_per_episode == 0)
        must_load_previous_run = false;
    } else if(can_continue && !learning && tepisode + 1 >= test_episode_per_episode){
      must_load_previous_run = false;
    }

    save_agent(agent, lepisode, learning);
  }

  void dump_and_display(unsigned int episode, unsigned int instance, unsigned int tepisode,
                        const std::list<double>& all_rewards, Environment* env,
                        Agent* ag, bool learning, uint step) {
    (void) all_rewards;
    bool display = episode % display_log_each == 0;
    bool dump = episode % dump_log_each == 0;

    if(!learning) {
      display = (episode+tepisode) % display_log_each == 0;
      dump = true; //unless why do you compute it ?
    }

    if (dump || display) {
//       bib::Utils::V3M reward_stats = bib::Utils::statistics(all_rewards);

      if (display && ((display_learning && learning) || !learning)) {
        bib::Dumper<Environment, bool, bool> env_dump(env, true, false);
        bib::Dumper<Agent, bool, bool> agent_dump(ag, true, false);
        LOG_INFO((learning ? "L " : "T ")
                 << std::left << std::setw(6) << std::setfill(' ') << episode
//                  << std::left << std::setw(7) << std::fixed << std::setprecision(3) << reward_stats.mean
//                  << std::left << std::setw(7) << std::fixed << std::setprecision(3) << reward_stats.var
//                  << std::left << std::setw(7) << std::fixed << std::setprecision(3) << reward_stats.max
//                  << std::left << std::setw(7) << std::fixed << std::setprecision(3) << reward_stats.min
                 << std::left << std::setw(7) << std::fixed << step
                 << " " << agent_dump << " " << env_dump);
      }

      if (dump) {
        bib::Dumper<Environment, bool, bool> env_dump(env, false, true);
        bib::Dumper<Agent, bool, bool> agent_dump(ag, false, true);
        LOG_FILE(learning ? std::to_string(instance) + DEFAULT_DUMP_LEARNING_FILE :
                 std::to_string(instance) + "." +std::to_string(tepisode) + DEFAULT_DUMP_TESTING_FILE,
                 episode << " " 
//                  << reward_stats.mean << " " << reward_stats.var << " " << reward_stats.max << " " << reward_stats.min << " " 
                 << step << " " << agent_dump << " " << env_dump);
      }
    }
  }

  void save_agent(Agent* agent, unsigned int episode, bool learning) {
    if (episode % save_agent_each == 0 && episode != 0) {
      std::string filename = learning ? DEFAULT_AGENT_SAVE_FILE : DEFAULT_AGENT_TEST_SAVE_FILE;
      std::string filename2 = std::to_string(episode);
      std::string path = filename + filename2;
      agent->save(path, save_best_agent, learning);
    }
  }
 private:
  void readCommandArgs(int argc, char** argv, string* s) {
    namespace po = boost::program_options;

    po::options_description desc("Allowed Simulator options");
    desc.add(Environment::program_options());
    desc.add(Agent::program_options());
    desc.add_options()
    ("config", po::value<std::vector<string>>(), "set the config file to load [default : config.ini]")
    ("save-best", "save only best params")
    ("continue", "process can be killed and run again")
    ("dpmt-sim", po::value<uint>(), "load n th simulator (only used with load and dpmt at the same time)")
    ("help", "produce help message");

    command_args = new po::variables_map;
    po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
    po::store(parsed, *command_args);
    po::notify(*command_args);

    if (command_args->count("help")) {
      std::cout << "Usage : Simulator [options]" << std::endl;
      std::cout << desc;
      exit(0);
    }

    if (command_args->count("config")) {
      *s = (*command_args)["config"].as<std::vector<string>>()[config_file_index];
    }

    save_best_agent = false;
    if (command_args->count("save-best")) {
      save_best_agent = true;
    }
    
    if(command_args->count("load") && command_args->count("dpmt-sim") &&
      (*command_args)["dpmt-sim"].as<uint>() != config_file_index){
      pass_this_sim = true;
    }

    can_continue = command_args->count("continue") > 0;
  }

  void readConfig(const string& config_file) {
    properties = new boost::property_tree::ptree;
    boost::property_tree::ini_parser::read_ini(config_file, *properties);

    max_episode                 = properties->get<unsigned int>("simulation.max_episode");
    test_episode_per_episode    = properties->get<int>("simulation.test_episode_per_episode");
    test_episode_at_end         = properties->get<unsigned int>("simulation.test_episode_at_end");

    dump_log_each               = properties->get<unsigned int>("simulation.dump_log_each");
    display_log_each            = properties->get<unsigned int>("simulation.display_log_each");
    save_agent_each             = properties->get<unsigned int>("simulation.save_agent_each");

    try {
      display_learning            = properties->get<bool>("simulation.display_learning");
    } catch(boost::exception const& ) {
      display_learning            = true;
    }
    
    continue_save_each          = DEFAULT_AGENT_SAVE_EACH_CONTINUE;
    try {
      continue_save_each        = properties->get<unsigned int>("simulation.continue_save_each");
      LOG_INFO("catch continue_save_each " << continue_save_each);
    } catch(boost::exception const& ) {
    }

#ifndef NDEBUG
    well_init = true;
#endif
  }

 private:
  struct simu_state {
    uint episode;
    
    friend class boost::serialization::access;
    template <typename Archive>
    void serialize(Archive& ar, const unsigned int) {
      ar& BOOST_SERIALIZATION_NVP(episode);
    }
  };
  unsigned int max_episode;
  int test_episode_per_episode;
  unsigned int test_episode_at_end;

  unsigned int dump_log_each;
  unsigned int display_log_each;
  unsigned int save_agent_each;

  bool display_learning, save_best_agent, can_continue;
  uint continue_save_each;
  bool must_load_previous_run = false;
  bool pass_this_sim = false;

 protected:
  uint config_file_index;
  boost::property_tree::ptree* properties;
  boost::program_options::variables_map* command_args;

  bib::Chrono time_spend;

  Environment* env;
  Agent* agent;

#ifndef NDEBUG
 private:
  bool well_init = false;
#endif
};
}  // namespace arch

#endif
