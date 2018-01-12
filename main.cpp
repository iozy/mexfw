#include <iostream>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>
#include <elle/reactor/http/Request.hh>
#include "exchanges.hpp"
#include "mexfw.hpp"
#include "goodies.hpp"
#include "cex.hpp"

using namespace elle::reactor;
using namespace rapidjson;
using namespace mexfw;
using namespace mexfw::utils;

int main(void) {
    Scheduler sched;


    Thread main_thread(sched, "main thread", [] {
        rest_api<CEX> x;
        x.load_proxies("proxies.json");
        std::cout << x.get_all_pairs() << '\n';
    });
    sched.run();
    return 0;
}
