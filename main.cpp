#include <iostream>
#include <fstream>
#include <functional>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>
#include <elle/reactor/http/Request.hh>
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

int main(int argc, char *argv[]) {
    Scheduler sched;
    sched.signal_handle(SIGINT, [&] { sched.terminate(); });
    Thread main_thread(sched, "main thread", [] {
        rest_api<TEST_PROXY> api;
        //arbitrage arb(CEX::fee);
        api.load_proxies();
        api.test_proxies("https://cex.io/api/currency_limits");
        //auto pairs = api.get_all_pairs();
        //std::cout<<"All pairs: "<<pairs<<'\n';
        //api.get_ob(pairs, arb);
        //for(auto p: arb.all_pairs()) {
        //    auto ob_e = arb.ob(p, 0);
        //    std::cout<<p<<": "<<ob_e.first<<" "<<ob_e.second<<'\n';
        //}
        api.print_proxy_stats();
        api.filter_proxies();
        api.save_proxies();
        std::cout<<"done\n";
    });

    try {
        sched.run();
    } catch(const std::runtime_error& e) {
        std::cout<<"Error: "<<e.what()<<std::endl;
        return -1;
    }

    return 0;
}
