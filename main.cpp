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
using namespace ctpl;

typedef CEX EXCHANGE;

int main(int argc, char *argv[]) {
    Scheduler sched;
    Barrier proxies_loaded;
    thread_pool tp(std::thread::hardware_concurrency());

    tp.push([](auto){
        std::this_thread::sleep_for(50s);
    });

    if(!file_exists("settings.json")) {
        std::cout<<"Failed to open settings.json\n";
        return -1;
    }

    auto settings = parse_file("settings.json");
    rest_api<EXCHANGE> api(settings["proxy_flood"].GetBool());
    arbitrage arb(EXCHANGE::fee);
    Thread proxy_thread(sched, "update proxies", [&] {
        while(true)
        {
            api.load_proxies();
            std::cout<<"Loading proxies is done ok.\n";
            proxies_loaded.open();
            sleep(30s);
        }
    });

    sched.signal_handle(SIGINT, [&] {
        std::cout<<"Exiting...\n";
        tp.clear_queue();
        tp.resize(0);
        sched.terminate(); 
    });

    Thread main_thread(sched, "main thread", [&] {
        proxies_loaded.wait();

        while(true)
        {
            auto pairs = api.get_all_pairs();
            std::cout<<"All pairs: "<<pairs<<'\n';
            api.get_ob(pairs, arb);

            for(auto p : arb.all_pairs()) {
                auto ob_e = arb.ob(p, 0);
                std::cout<<p<<": "<<arb.ob(p)<<'\n';
            }

            auto cycles = arb.find_cycles();

            if(cycles.size()) std::cout<<"found: \n";

            for(auto c : cycles) {
                std::cout<<arb.cycle2string(c)<<'\n';
            }

            sleep(5s);
        }
    });

    try {
        sched.run();
        std::cout<<"done\n";
    } catch(const std::runtime_error& e) {
        std::cout<<"Error: "<<e.what()<<std::endl;
        return -1;
    }

    return 0;
}
