#include <iostream>
#include <fstream>
#include <functional>
#include <boost/algorithm/cxx11/any_of.hpp>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/signal.hh>
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
using boost::algorithm::any_of_equal;

typedef CCEX EXCHANGE;

int main(int argc, char *argv[]) {
    typedef std::unordered_map<std::string, std::pair<double, double>> cycleSize_t;

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
        while(!sched.done())
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
        sched.run_later("terminator", [&]{ sched.terminate_later(); });
        
    });
    Thread main_thread(sched, "main thread", [&] {
        elle::With<Scope>() << [&](Scope & scope) {
            api.load_keys();
            api.load_nonces();
            proxies_loaded.wait();
            std::unordered_map<std::string, double> balance;

            Signal sbu, sfo;
            //std::cout<<"pairs="<<api.get_all_pairs()<<'\n';
            //std::cout<<"opened orders: "<<api.get_open_orders()<<'\n';
            
            //scope.run_background("terminator", [&]{ sleep(1s); sched.terminate_now(); });
                    
            scope.run_background("update_balance", [&]{ 
                while(!sched.done()) {
                    api.get_all_pairs();
                    std::cout<<"-----------------------got all pairs\n";
                    sleep(1s);
                }
            });

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
