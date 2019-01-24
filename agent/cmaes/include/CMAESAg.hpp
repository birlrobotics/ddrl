#ifndef CMAESAG_HPP
#define CMAESAG_HPP

#include <vector>
#include <string>
#include <type_traits>
#include <boost/serialization/list.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/functional/hash.hpp>

#include "arch/ARLAgent.hpp"
#include "bib/Seed.hpp"
#include "bib/Utils.hpp"
#include <bib/MetropolisHasting.hpp>
#include <bib/XMLEngine.hpp>
#include <bib/Combinaison.hpp>
#include "nn/MLP.hpp"
#include "nn/DevMLP.hpp"
#include "nn/DODevMLP.hpp"
#include "cmaes_interface.h"

template<typename NN = MLP>
class CMAESAg : public arch::ARLAgent<arch::AgentProgOptions> {
 public:
  CMAESAg(unsigned int _nb_motors, unsigned int _nb_sensors)
    : arch::ARLAgent<arch::AgentProgOptions>(_nb_motors, _nb_sensors), nb_sensors(_nb_sensors) {

  }

  virtual ~CMAESAg() {
    cmaes_exit(evo);
    delete evo;
    delete ann;
    delete hidden_unit_a;
  }

  const std::vector<double>& _run(double, const std::vector<double>& sensors,
                                  bool, bool, bool) override {

    vector<double>* next_action = ann->computeOut(sensors);

//  CMA-ES already implement exploration in parameter space
    last_action.reset(next_action);

    return *next_action;
  }


  void _unique_invoke(boost::property_tree::ptree* pt, boost::program_options::variables_map* vm) override {
    hidden_unit_a               = bib::to_array<uint>(pt->get<std::string>("agent.hidden_unit_a"));
    actor_hidden_layer_type     = pt->get<uint>("agent.actor_hidden_layer_type");
    actor_output_layer_type     = pt->get<uint>("agent.actor_output_layer_type");
    batch_norm                  = pt->get<uint>("agent.batch_norm");
    population                  = pt->get<uint>("agent.population");
    initial_deviation           = pt->get<double>("agent.initial_deviation");

    check_feasible = true;
    ignore_null_lr = true;
    racing = false;
    error_count = 0;
    xbestever_score = std::numeric_limits<double>::max();

    try {
      check_feasible = pt->get<bool>("agent.check_feasible");
    } catch(boost::exception const& ) {
    }

    try {
      ignore_null_lr = pt->get<bool>("agent.ignore_null_lr");
    } catch(boost::exception const& ) {
    }

    try {
      racing = pt->get<bool>("agent.racing");
    } catch(boost::exception const& ) {
    }

    episode = 0;

    ann = new NN(nb_sensors, *hidden_unit_a, nb_motors, 0.1, 1, actor_hidden_layer_type, actor_output_layer_type,
                 batch_norm);
    if(std::is_same<NN, DevMLP>::value)
      ann->exploit(pt, static_cast<CMAESAg *>(old_ag)->ann);
    else if(std::is_same<NN, DODevMLP>::value)
      ann->exploit(pt, nullptr);

//     const uint dimension = (nb_sensors+1)*hidden_unit_a->at(0) + (hidden_unit_a->at(0)+1)*nb_motors;
    const uint dimension = ann->number_of_parameters(ignore_null_lr);
    double* startx  = new double[dimension];
    double* deviation  = new double[dimension];
    for(uint j=0; j< dimension; j++) {
      deviation[j] = initial_deviation;
    }
    ann->copyWeightsTo(startx, ignore_null_lr);

    evo = new cmaes_t;
    arFunvals = cmaes_init(evo, dimension, startx, deviation, 0, population, NULL/*"config.cmaes.ini"*/);
    delete[] startx;
    delete[] deviation;
//     evo->sp.stopTolFun = 1e-150;
//     evo->sp.stopTolFunHist = 1e-150;
//     evo->sp.stopTolUpXFactor = 1e50;

    printf("%s\n", cmaes_SayHello(evo));
    new_population();
    LOG_DEBUG(cmaes_Get(evo, "lambda") << " " << dimension << " " << population << " " << cmaes_Get(evo, "N"));
    if (population < 2)
      LOG_DEBUG("population too small, changed to : " << (4+(int)(3*log((double)dimension))));

    if(vm->count("continue") > 0) {
      uint continue_save_each          = DEFAULT_AGENT_SAVE_EACH_CONTINUE;
      try {
        continue_save_each            = pt->get<uint>("simulation.continue_save_each");
      } catch(boost::exception const& ) {
      }

      if(continue_save_each % (int) cmaes_Get(evo, "lambda") != 0) {
        LOG_ERROR("continue_save_each must be a multiple of the population size !");
        exit(1);
      }
    }
    
    if(std::is_same<NN, DODevMLP>::value){
      try {
        if(pt->get<double>("devnn.ewc") >= 0.){
          LOG_ERROR("CMA-ES doesn't rely on gradient (difficult to compute Fisher Matrix)");
          exit(1);
        }
      } catch(boost::exception const& ) {
      }
    }
  }

  bool is_feasible(const double* parameters) {
    for(uint i=0; i < (uint) cmaes_Get(evo, "dim"); i++)
      if(fabs(parameters[i]) >= 500.f) {
        return false;
      }

    return true;
  }

  void new_population() {
    const char * terminate =  cmaes_TestForTermination(evo);
    if(terminate) {
      LOG_INFO("mismatch "<< terminate);
      error_count++;

      if(error_count > 20 && racing) {
        LOG_FILE(DEFAULT_END_FILE, "-1");
        exit(0);
      }
    }
    //ASSERT(!cmaes_TestForTermination(evo), "mismatch "<< cmaes_TestForTermination(evo));

    current_individual = 0;
    pop = cmaes_SamplePopulation(evo);

    if(check_feasible) {
      //check that the population is feasible
      bool allfeasible = true;
      for (int i = 0; i < cmaes_Get(evo, "popsize"); ++i)
        while (!is_feasible(pop[i])) {
          cmaes_ReSampleSingle(evo, i);
          allfeasible = false;
        }

      if(!allfeasible)
        LOG_INFO("non feasible solution produced");
    }
  }

  void start_instance(bool learning) override {
    last_action = nullptr;
    scores.clear();

    if(std::is_same<NN, DODevMLP>::value && learning) {
      auto dodevmlp = static_cast<DODevMLP *>(ann);
      bool reset_operator, changed;
      std::tie(reset_operator, changed) = dodevmlp->inform(episode, last_sum_weighted_reward);
      if(reset_operator && changed) {
        LOG_INFO("reset learning catched");

        if(!dodevmlp->ewc_enabled()){
          const double* parameters = nullptr;
          parameters = getBestSolution();
          loadPolicyParameters(parameters);
        }

        cmaes_exit(evo);
        delete evo;

        const uint dimension = ann->number_of_parameters(ignore_null_lr);
        double* startx  = new double[dimension];
        double* deviation  = new double[dimension];
        for(uint j=0; j< dimension; j++) {
          deviation[j] = initial_deviation;
        }
        ann->copyWeightsTo(startx, ignore_null_lr);

        xbestever_score = std::numeric_limits<double>::max();
        cmaes_UpdateDistribution_done_once = false;
        evo = new cmaes_t;
        arFunvals = cmaes_init(evo, dimension, startx, deviation, 0, population, NULL/*"config.cmaes.ini"*/);
        delete[] startx;
        delete[] deviation;
        new_population();
      }
    }

    if(!justLoaded) {
      //put individual into NN
      const double* parameters = nullptr;
      if(learning || !cmaes_UpdateDistribution_done_once)
        parameters = pop[current_individual];
      else
        parameters = getBestSolution();

      loadPolicyParameters(parameters);
    }

    if(learning)
      episode++;
    //LOG_FILE("policy_exploration", ann->hash());
  }

  void restoreBest() override {
    const double* parameters = getBestSolution();
    loadPolicyParameters(parameters);
  }

  void end_episode(bool) override {
    scores.push_back(-sum_weighted_reward + ann->ewc_cost());
  }

  void end_instance(bool learning) override {
    if(learning) {
      arFunvals[current_individual] = std::accumulate(scores.begin(), scores.end(), 0.f) / scores.size();
      
      //TODO with instance
      ann->update_best_param_previous_task(sum_weighted_reward);

      current_individual++;
      if(current_individual >= cmaes_Get(evo, "lambda")) {
        cmaes_UpdateDistribution(evo, arFunvals);
        cmaes_UpdateDistribution_done_once=true;
        new_population();
      }
    }

    justLoaded = false;
  }

  void save(const std::string& path, bool save_best, bool) override {
    if(!save_best || !cmaes_UpdateDistribution_done_once) {
      ann->save(path+".actor");
    } else if(save_best && -sum_weighted_reward < xbestever_score ) {
      xbestever_score = -sum_weighted_reward;
      ann->save(path+".actor");
    }
//     else {
//       //TODO : check this part, apparently it modify the learning
//       LOG_WARNING("be careful it might be a problem here");
//       NN* to_be_restaured = new NN(*ann, false);
//       const double* parameters = getBestSolution();
//       loadPolicyParameters(parameters);
//       ann->save(path+".actor");
//       delete ann;
//       ann = to_be_restaured;
//     }
  }

  void load(const std::string& path) override {
    justLoaded = true;
    ann->load(path+".actor");
  }

  void save_run() override {
    if(current_individual == 1) {
      const double* xbptr_ = getBestSolution();
      std::vector<double> xbestever_((int)cmaes_Get(evo, "N"));
      xbestever_.assign(xbptr_, xbptr_ + (int)cmaes_Get(evo, "N"));
      double bs = getBestScore();

      struct algo_state st = {scores, justLoaded, cmaes_UpdateDistribution_done_once,
               current_individual, episode, error_count, bs, xbestever_
      };
      bib::XMLEngine::save(st, "algo_state", "continue.algo_state.data");
      cmaes_WriteToFile(evo, "resume", "continue.cmaes.data");
    }
  }

  void load_previous_run() override {
    auto algo_state_ = bib::XMLEngine::load<struct algo_state>("algo_state", "continue.algo_state.data");
    scores = algo_state_->scores;
    justLoaded = algo_state_->justLoaded;
    cmaes_UpdateDistribution_done_once = algo_state_->cmaes_UpdateDistribution_done_once;
    current_individual = algo_state_->current_individual;
    episode = algo_state_->episode;
    error_count = algo_state_->error_count;
    xbestever_score = algo_state_->xbestever_score;
    xbestever_ptr = algo_state_->xbestever_ptr;
    delete algo_state_;
    char file_[] = "continue.cmaes.data";
    cmaes_resume_distribution(evo, file_);
    new_population();
  }

  MLP* getNN() {
    return ann;
  }

 protected:
  void _display(std::ostream& out) const override {
    out << std::setw(8) << std::fixed << std::setprecision(5) << sum_weighted_reward << " " << ann->ewc_cost();
  }

  void _dump(std::ostream& out) const override {
    out << std::setw(8) << std::fixed << std::setprecision(5) << sum_weighted_reward;
  }

 private:
  void loadPolicyParameters(const double* parameters) {
    ann->copyWeightsFrom(parameters, ignore_null_lr);
  }

  const double* getBestSolution() {
    //TODO:
//     LOG_DEBUG(cmaes_Get(evo, "fbestever")  << " " << xbestever_score);
    if(cmaes_Get(evo, "fbestever") < xbestever_score)
      return cmaes_GetPtr(evo, "xbestever");
    else
      return xbestever_ptr.data();
  }

  double getBestScore() {
    return std::min(cmaes_Get(evo, "fbestever"), xbestever_score);
  }

 private:
  //initilized by constructor
  uint nb_sensors;

  //initialized by invoke
  std::vector<uint>* hidden_unit_a;
  uint population, actor_hidden_layer_type, actor_output_layer_type, batch_norm;
  double initial_deviation;
  bool check_feasible;
  bool ignore_null_lr;
  bool racing;
  MLP* ann;
  cmaes_t* evo;
  double *arFunvals;

  //internal mecanisms
  std::shared_ptr<std::vector<double>> last_action;
  std::list<double> scores;
  double *const *pop;
  bool justLoaded = false;
  bool cmaes_UpdateDistribution_done_once = false;
  uint current_individual;
  uint episode;
  uint error_count;
  double xbestever_score;
  std::vector<double> xbestever_ptr;

  struct algo_state {
    std::list<double> scores;
    bool justLoaded;
    bool cmaes_UpdateDistribution_done_once;
    uint current_individual;
    uint episode;
    uint error_count;
    double xbestever_score;
    std::vector<double> xbestever_ptr;

    friend class boost::serialization::access;
    template <typename Archive>
    void serialize(Archive& ar, const unsigned int) {
      ar& BOOST_SERIALIZATION_NVP(scores);
      ar& BOOST_SERIALIZATION_NVP(justLoaded);
      ar& BOOST_SERIALIZATION_NVP(cmaes_UpdateDistribution_done_once);
      ar& BOOST_SERIALIZATION_NVP(current_individual);
      ar& BOOST_SERIALIZATION_NVP(episode);
      ar& BOOST_SERIALIZATION_NVP(error_count);
      ar& BOOST_SERIALIZATION_NVP(xbestever_score);
      ar& BOOST_SERIALIZATION_NVP(xbestever_ptr);
    }
  };
};

#endif

