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
#include "ccex.hpp"
#include "test_proxy.hpp"
#include "arbitrage.hpp"

using namespace elle::reactor;
using namespace rapidjson;
using namespace mexfw;
using namespace mexfw::utils;
using namespace arbtools;
using namespace arbtools::misc;
using namespace ctpl;

typedef CCEX EXCHANGE;

int main(int argc, char *argv[]) {
    Scheduler sched;
    Barrier proxies_loaded;
    //thread_pool tp(std::thread::hardware_concurrency());

    if(!file_exists("settings.json")) {
        std::cout << "Failed to open settings.json\n";
        return -1;
    }

    auto settings = parse_file("settings.json");
    rest_api<EXCHANGE> api(settings["proxy_flood"].GetBool());
    arbitrage arb(EXCHANGE::fee);
    Thread proxy_thread(sched, "update proxies", [&] {
        while(true)
        {
            api.load_proxies();
            //std::cout << "Loading proxies is done ok.\n";
            proxies_loaded.open();
            sleep(30s);
        }
    });
    sched.signal_handle(SIGINT, [&] {
        std::cout << "Exiting...\n";
        //tp.clear_queue();
        //tp.resize(0);
        sched.terminate();
    });
    Thread main_thread(sched, "main thread", [&] {
        api.load_keys();
        api.load_nonces();
        proxies_loaded.wait();
        std::unordered_map<std::string, double> balance;

        //std::cout<<"pairs="<<api.get_all_pairs()<<'\n';
        //std::cout<<"opened orders: "<<api.get_open_orders()<<'\n';
        while(true) {
            api.get_full_ob(arb);
            for(auto cycle: arb.find_cycles()) {
                std::cout<<cycle2string(cycle)<<'\n';
            }
            api.update_balance(balance);
            std::cout<<"balance:"<<balance<<'\n';
            api.save_nonces();
            sleep(1s);
        }
        sched.terminate();
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
