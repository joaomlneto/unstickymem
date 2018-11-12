
#include <unistd.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>

#include <iostream>

#include <boost/program_options.hpp>

#include "unstickymem/PerformanceCounters.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/MemoryMap.hpp"
#include "unstickymem/mode/FixedRatioMode.hpp"
#include "unstickymem/Logger.hpp"

namespace po = boost::program_options;

namespace unstickymem {

static Mode::Registrar<FixedRatioMode>
  registrar(FixedRatioMode::name(), FixedRatioMode::description());

po::options_description FixedRatioMode::getOptions() {
  po::options_description mode_options("Fixed Ratio mode parameters");
  mode_options.add_options()
    (
      "UNSTICKYMEM_LOCAL_RATIO",
      po::value<double>(&_local_ratio)->default_value(1.0),
      "Percentage of pages to put in the worker nodes"
    )
    (
      "UNSTICKYMEM_NUM_POLLS",
      po::value<unsigned int>(&_num_polls)->default_value(20),
      "How many measurements to make for each placement ratio"
    )
    (
      "UNSTICKYMEM_NUM_POLL_OUTLIERS",
      po::value<unsigned int>(&_num_poll_outliers)->default_value(5),
      "How many of the top-N and bottom-N measurements to discard"
    )
    (
      "UNSTICKYMEM_POLL_SLEEP",
      po::value<useconds_t>(&_poll_sleep)->default_value(200000),
      "Time (in microseconds) between measurements"
    )
    (
      "UNSTICKYMEM_EXIT_WHEN_FINISHED",
      po::value<bool>(&_exit_when_finished)->default_value(false),
      "Time (in microseconds) between measurements"
    )
  ;
  return mode_options;
}

void FixedRatioMode::printParameters() {
  LINFOF("UNSTICKYMEM_LOCAL_RATIO:        %lf", _local_ratio);
  LINFOF("UNSTICKYMEM_WAIT_START:         %lu", _wait_start);
  LINFOF("UNSTICKYMEM_NUM_POLLS:          %lu", _num_polls);
  LINFOF("UNSTICKYMEM_NUM_POLL_OUTLIERS:  %lu", _num_poll_outliers);
  LINFOF("UNSTICKYMEM_POLL_SLEEP:         %lu", _poll_sleep);
  LINFOF("UNSTICKYMEM_EXIT_WHEN_FINISHED: %s",
         _exit_when_finished ? "Yes" : "No");
}

void FixedRatioMode::start() {
  printParameters();
  LFATAL("Fixed Ratio Mode: starting");

  // set default memory policy to interleaved
  LDEBUG("Setting default memory policy to interleaved");
  set_mempolicy(MPOL_INTERLEAVE,
                numa_get_mems_allowed()->maskp,
                numa_get_mems_allowed()->size);

  place_all_pages(_local_ratio);
  unstickymem_log(_local_ratio);

  // compute stall rate
  double stall_rate = get_average_stall_rate(_num_polls, _poll_sleep,
  _num_poll_outliers);
  fprintf(stderr, "measured stall rate: %lf\n",
          get_average_stall_rate(_num_polls, _poll_sleep, _num_poll_outliers));

  // print stall_rate to a file for debugging!
  unstickymem_log(stall_rate, _local_ratio);
  pthread_exit(0);
}

}  // namespace unstickymem
