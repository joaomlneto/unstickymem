/**
 * Copyright 2018 João Neto
 * This is the shared runtime state between the different threads
 **/
#include "fpthread/Logger.hpp"

fpthread::Logger Logger;
fpthread::Logger *L = &Logger;
