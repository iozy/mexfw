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
    Barrier proxies_loaded;
    //thread_pool tp(std::thread::hardware_concurrency());

    if(!file_exists("settings.json")) {
        std::cout << "Failed to open settings.json\n";
        return -1;
    }

    auto settings = parse_file("settings.json");
    rest_api<EXCHANGE> api("up104099398", settings["proxy_flood"].GetBool());
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
        std::unordered_map<std::string, size_t> hashes;
        std::vector<std::string> slow_pool, fast_pool;
        std::unordered_map<std::string, double> bal;
        std::cout << "getting all pairs\n";
        fast_pool = api.get_all_pairs();
        api.get_ob(fast_pool, arb);
        std::cout << "saving hashes\n";

        for(const auto& p : fast_pool) {
            hashes[p] = arb(as_pair(p)).hash;
        }
        sleep(5s);
        std::cout << "getting all pairs\n";
        fast_pool = api.get_all_pairs();
        api.get_ob(fast_pool, arb);
        std::cout << "partitionize\n";
        auto sp_begin = std::partition(fast_pool.begin(), fast_pool.end(), [&](const auto & p) {
            return hashes[p] != arb(as_pair(p)).hash;
        });
        std::cout << "movin\n";
        std::move(sp_begin, fast_pool.end(), std::back_inserter(slow_pool));
        std::cout << "erasin\n";
        fast_pool.erase(sp_begin, fast_pool.end());

        elle::With<Scope>() << [&](Scope& scope) {
            scope.run_background("slow_pool", [&] {
                while(true) {
                    api.get_ob(slow_pool, arb);
                    auto fp_begin = std::partition(slow_pool.begin(), slow_pool.end(), [&](const auto & p) {
                        bool x = hashes[p] == arb(as_pair(p)).hash;
                        hashes[p] = arb(as_pair(p)).hash;
                        return x;
                    });
                    std::move(fp_begin, slow_pool.end(), std::back_inserter(fast_pool));
                    slow_pool.erase(fp_begin, slow_pool.end());
                    sleep(10s);
                }
            });
            scope.run_background("fast_pool", [&] {
                while(true) {
                    api.get_ob(fast_pool, arb);
                    auto sp_begin = std::partition(fast_pool.begin(), fast_pool.end(), [&](const auto & p) {
                        bool x = hashes[p] != arb(as_pair(p)).hash;
                        hashes[p] = arb(as_pair(p)).hash;
                        return x;
                    });
                    std::move(sp_begin, fast_pool.end(), std::back_inserter(slow_pool));
                    fast_pool.erase(sp_begin, fast_pool.end());
                    sleep(2s);
                }
            });
            scope.run_background("print_all", [&] {
                bool traded = false;
                while(true) {
                    /*std::cout << "slow_pool: " << slow_pool.size() << '\t';
                    std::cout << "fast_pool: " << fast_pool.size() << '\t';
                    std::cout << "S: " << fast_pool.size() + slow_pool.size() << '\n';*/
                    //arb.recalc_rates(pair, true);
                    std::string pair = "ETH-USD";
                    //std::cout<<std::left<<std::setw(20)<<"bids"<<std::setw(20)<<"asks"<<std::setw(20)<<"asks"<<'\n';
                    //for(size_t i = 0; i < 5; ++i) {
                    //    std::cout<<std::left<<std::setw(20)<<gain_r(arb.ob_size(pair, i))<<std::setw(20)<<gain_r(arb.ob_size(pair, -i, true))<<std::setw(20)<<gain_r(arb.ob_size("USD-BTC", i))<<'\n';
                    //}
                    //std::cout<<'\n';
                    auto cycles = arb.find_cycles();
                    std::cout<<cycles<<'\n';
                    std::cout<<"current rate="<<arb(pair).rate<<" weight="<<arb(pair).weight<<'\n';
                    arb.change_rate(pair, gain_r(arb.ob_size(pair, 10, true)));
                    std::cout<<"current rate="<<arb(pair).rate<<" weight="<<arb(pair).weight<<'\n';
                    cycles = arb.find_cycles();
                    std::cout<<cycles<<'\n';

                    if(cycles.size() > 0) {
                        for(auto c : cycles) {
                            std::cout << cycle2string(c) << "\tgain="<<gain(arb, c)<<'\n';
                            for(auto p: c) {
                                auto sz = arb.sizing(c, p, arb.ob_size(p, 0));
                                std::cout << print_sizes(arb, c, sz)<<" profit="<<calc_profit(c, sz)<<'\n';                                
                            }
                        }
                    }
                    api.update_balance(bal);
                    /*for(auto b: bal) {
                        std::cout<<b.first<<"="<<b.second<<" ";
                    }
                    std::cout<<'\n';*/
                    
                    api.save_nonces();

                    sleep(7s);
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
