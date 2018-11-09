#ifndef UNSTICKYMEM_ADAPTIVEMODE_HPP_
#define UNSTICKYMEM_ADAPTIVEMODE_HPP_

#include "unstickymem/mode/Mode.hpp"

namespace unstickymem {

class AdaptiveMode : public Mode {
 public:
  static std::string name() {
    return "adaptive";
  }

  static std::string description() {
    return "Look for optimal local/remote page placement";
  }

  static std::unique_ptr<Mode> createInstance() {
   return std::make_unique<AdaptiveMode>();
  }

  po::options_description getOptions() {
    po::options_description options("Adaptive Mode Options");
    options.add_options()
      ("dummy", po::value<std::string>(), "placeholder! FIXME!")
    ;
    return options;
  }

  void start();
};

}  // namespace unstickymem

#endif  // UNSTICKYMEM_ADAPTIVEMODE_HPP_
