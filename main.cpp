#include <iostream>
#include <fstream>
#include <functional>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/Channel.hh>
#include <elle/reactor/http/Request.hh>
#include <ctpl.h>
#include "exchanges.hpp"
#include "mexfw.hpp"
#include "goodies.hpp"
#include "cex.hpp"
#include "test_proxy.hpp"
#include "arbitrage.hpp"

using namespace elle::reactor;
using namespace rapidjson;
using namespace mexfw;
using namespace mexfw::utils;
using namespace arbtools;
using namespace arbtools::misc;
using namespace ctpl;

typedef CEX EXCHANGE;

int main(int argc, char *argv[]) {
    Scheduler sched;
    sched.signal_handle(SIGINT, [&] {
        std::cout << "Exiting...\n";
        sched.terminate();
    });
    Thread main_thread(sched, "main thread", [&] {
        elle::With<Scope>() << [&] (Scope& scope) {
            scope.run_background("", []{});
            scope.wait();
        };
    });

    try {
        sched.run();
        std::cout << "done\n";
    } catch(const std::runtime_error& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
