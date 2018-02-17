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
            /*std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<" "<<arb.ob("mcap-btc", 0)<<" "<<arb.ob("mcap-btc", 0, true)<<'\n';
            api.get_ob({"mcap-btc"}, arb);
            std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<" "<<arb.ob("mcap-btc", 0)<<" "<<arb.ob("mcap-btc", 0, true)<<'\n';
            arb.change_rate("mcap-btc", gain_r(arb.ob_size("mcap-btc",0, true)));
            std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<'\n';
            arb.change_rate("mcap-btc", gain_r(arb.ob_size("mcap-btc",0, false)));
            std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<'\n';*/
            auto cycles = arb.find_cycles();
            std::cout<<"Found "<<cycles.size()<<" cycles\n";
            api.get_ob(arb.extract_unique_pairs(cycles), arb);
            for(auto cycle: cycles) {
                std::cout<<">>> CYCLE: "<<cycle2string(cycle)<<" gain="<<gain(arb, cycle)<<'\n';
                //for(auto p_it = cycle.begin(); p_it != std::prev(cycle.end()); ++p_it) {
                for(size_t i = 0; i < cycle.size(); ++i) {
                    auto p = *cycle.begin();
                    arb.change_rate(p, gain_r(arb.ob_size(p, 0, true)));
                    for(auto nxt_p = std::next(cycle.begin()); nxt_p != cycle.end(); ++nxt_p) {
                        for(size_t j = 0; j < arb(p).ob.size(); ++j) {
                            auto sizes = arb.sizing_fixed(cycle, *nxt_p, arb.ob_size(*nxt_p, j), {{p, gain_r(arb.ob_size(p, 0, true))}});
                            arb.minsize(cycle, sizes);
                            arb.align_sizes(cycle, sizes);
                            double profit = calc_profit(cycle, sizes);
                            if(profit>0 && arb.is_aligned(cycle, sizes)) {
                                std::cout<<"locked "<<p<<", @rate="<<arb(p).rate<<", maximizing "<<*nxt_p<<", row="<<j<<" ->"<<arb.ob_size(*nxt_p, j)<<" gain="<<gain(arb, cycle)<<" real_gain="<<calc_gain(cycle, sizes)<<"\n";
                                std::cout<<print_sizes(arb, cycle, sizes)<<" profit="<<profit<<'\n';
                            }
                        }

                    }
                    
                    arb.change_rate(p, gain_r(arb.ob_size(p, 0, false)));
                    std::rotate(cycle.begin(), std::next(cycle.begin()), cycle.end());
                }
                std::cout<<"DOMS:\n";
                for(auto p: cycle) {
                    for(size_t i = 0; i < arb(p).ob.size(); ++i) {
                        if(i == 0) std::cout<<p<<": "<<arb.ob(p, 0, true)<<" %in sz% "<<arb.ob_size(p, 0, true)<<"\n-----------------\n";
                        std::cout<<p<<": "<<arb.ob(p, i)<<" %in sz% "<<arb.ob_size(p, i)<<'\n';
                    }
                    std::cout<<'\n';
                }
                //break;
                std::cout<<'\n';
            }
            //api.update_balance(balance);
            std::cout<<"balance:"<<api.get_balance()<<'\n'<<'\n';
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
