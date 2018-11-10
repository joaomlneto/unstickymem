#include <algorithm>
#include <iostream>

#include <boost/exception/diagnostic_information.hpp>

#include "better-enums/enum.h"

#include "unstickymem/mode/Mode.hpp"
#include "unstickymem/Runtime.hpp"
#include "unstickymem/Logger.hpp"

namespace unstickymem {

Runtime::Runtime() {
  loadConfiguration();
  printConfiguration();
  startSelectedMode();
}

void Runtime::loadConfiguration() {
  bool option_help;
  std::string option_loglevel;

  // library-level options
  po::options_description lib_options("Library Options");
  lib_options.add_options()
    (
      "UNSTICKYMEM_HELP",
      po::value<bool>(&option_help)->default_value(false),
      "Prints library options"
    )
    (
      "UNSTICKYMEM_MODE",
      po::value<std::string>(&_mode_name)->default_value("scan"),
      "The algorithm to be ran"
    )
    (
      "UNSTICKYMEM_LOGLEVEL",
      po::value<std::string>(&option_loglevel)->default_value("info"),
      "Log level (trace, debug, info, warn, error, fatal, off)"
    )
  ;

  // load library options from environment
  po::variables_map lib_env;
  po::store(po::parse_environment(lib_options, [lib_options](const std::string& var) {
    return std::any_of(
      lib_options.options().cbegin(),
      lib_options.options().cend(),
      [var](auto opt) { return var == opt->long_name(); }) ? var : "";
  }), lib_env);
  po::notify(lib_env);

  // get options of selected mode
  _mode = Mode::getMode(_mode_name);
  po::options_description mode_options = _mode->getOptions();

  // put all the options together
  po::options_description all_options("unstickymem options");
  all_options.add(lib_options).add(mode_options);

  // parse all options
  po::variables_map env;
  po::store(po::parse_environment(all_options, [all_options](const std::string& var) {
    return std::any_of(
      all_options.options().cbegin(),
      all_options.options().cend(),
      [var](auto opt) { return var == opt->long_name(); }) ? var : "";
  }), env);
  po::notify(env);

  // check if user is in trouble!
  if (option_help) {
     std::cout << std::endl << all_options << std::endl;
     exit(0);
  }

  // set log level
  L->loglevel(option_loglevel);

}

void Runtime::printConfiguration() {
  LINFOF("Mode: %s", _mode_name.c_str());
}

void Runtime::startSelectedMode() {
  _mode->start();
}

}  // namespace unstickymem
