#ifndef UNSTICKYMEM_RUNTIME_HPP_
#define UNSTICKYMEM_RUNTIME_HPP_

#include <boost/program_options.hpp>

#include "unstickymem/mode/Mode.hpp"

namespace po = boost::program_options;

namespace unstickymem {

class Runtime {
 private:
  std::string           _mode_name;
  std::unique_ptr<Mode> _mode;

 public:
  Runtime();
  po::options_description getOptions();
  void loadConfiguration();
  void printUsage();
  void printConfiguration();
  void startSelectedMode();
};

}  // namespace unstickymem

#endif  // UNSTICKYMEM_RUNTIME_HPP_
