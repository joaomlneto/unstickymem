#include <algorithm>
#include <iostream>

#include <boost/exception/diagnostic_information.hpp>

#include "better-enums/enum.h"

#include "unstickymem/mode/Mode.hpp"
#include "unstickymem/Runtime.hpp"
#include "unstickymem/Logger.hpp"

namespace unstickymem {

static int testing = -1;

Runtime::Runtime() {
  loadConfiguration();
  printConfiguration();
  startSelectedMode();
}

po::options_description Runtime::getOptions() {
  po::options_description lib_options("Library Options");
  lib_options.add_options()
    // (
    //   "UNSTICKYMEM_HELP",
    //   po::value<bool>(&option_help)->default_value(false),
    //   "The algorithm to be ran"
    // )
    (
      "UNSTICKYMEM_MODE",
      po::value<std::string>(&_mode)->default_value("scan"),
      "The algorithm to be ran"
    )
    (
      "UNSTICKYMEM_TEST",
      po::value<int>(&testing)->default_value(42),
      "Testing value"
    )
  ;
  return lib_options;
}

void Runtime::loadConfiguration() {
  // library-level options
  po::options_description lib_options = getOptions();

  // load "UNSTICKYMEM_MODE" from environment
  _mode = "scan";
  // po::variables_map lib_env;
  // po::store(po::parse_environment(lib_options, [lib_options](const std::string& var) {
  //   return std::any_of(
  //     lib_options.options().cbegin(),
  //     lib_options.options().cend(),
  //     [var](auto opt) { return var == opt->long_name(); }) ? var : "";
  // }), lib_env);
  // po::notify(lib_env);

  // get options of selected mode
  po::options_description mode_options = Mode::getMode(_mode)->getOptions();

  // put all the options together
  po::options_description all_options("unstickymem options");
  all_options.add(lib_options).add(mode_options);

  std::cout << std::endl << all_options << std::endl << std::endl;

  for (auto & opt : all_options.options()) {
    // LWARNF("option: %s", opt->format_parameter().c_str());
    LWARNF("option: %s", opt->long_name().c_str());
  }

  // parse all options
  try {
    po::variables_map env;
    po::store(po::parse_environment(all_options, [all_options](const std::string& var) {
      return std::any_of(
        all_options.options().cbegin(),
        all_options.options().cend(),
        [var](auto opt) { printf("checking %s == %s\n", var.c_str(), opt->long_name().c_str()); if (var == opt->long_name()) printf("YAY!!!\n"); return var == opt->long_name(); }) ? var : "";
    }), env);
    po::notify(env);
  } catch (...) {
    std::exception_ptr p = std::current_exception();
    std::clog <<(p ? p.__cxa_exception_type()->name() : "null") << std::endl;
    std::clog << boost::current_exception_diagnostic_information() << std::endl;
  }

  printf("testing value: %d\n", testing);

}

void Runtime::printConfiguration() {
  LINFOF("Selected mode: %s", _mode.c_str());
}

void Runtime::startSelectedMode() {
  std::unique_ptr<unstickymem::Mode> a = Mode::getMode(_mode);
  a->start();
}

}  // namespace unstickymem
