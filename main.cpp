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
        sched.terminate();
    });
    Thread main_thread(sched, "main thread", [&] {
        elle::With<Scope>() << [&](Scope & scope) {
            api.load_keys();
            api.load_nonces();
            proxies_loaded.wait();
            std::unordered_map<std::string, double> balance, b0;

            Signal sbu, sfo;
            //std::cout<<"pairs="<<api.get_all_pairs()<<'\n';
            //std::cout<<"opened orders: "<<api.get_open_orders()<<'\n';
            /*for(auto b: EXCHANGE::bases) {
                std:: cout<<b<<" ";
            }
            std::cout<<'\n';*/
            
            scope.run_background("update_balance", [&]{ 
                while(!sched.done()) {
                    api.update_balance(balance); 
                    sbu.signal();
                    sleep(1s);
                }
            });
            scope.run_background("get_full_ob",[&]{
                while(!sched.done()) {
                    api.get_full_ob(arb);
                    sfo.signal();
                    sleep(1s);
                }
            });
            scope.run_background("get_init_balance", [&]{
                sbu.wait();
                b0 = balance;
            });
            while(true) {
                std::unordered_map<std::string, cycleSize_t> cs;
                /*std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<" "<<arb.ob("mcap-btc", 0)<<" "<<arb.ob("mcap-btc", 0, true)<<'\n';
                api.get_ob({"mcap-btc"}, arb);
                std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<" "<<arb.ob("mcap-btc", 0)<<" "<<arb.ob("mcap-btc", 0, true)<<'\n';
                arb.change_rate("mcap-btc", gain_r(arb.ob_size("mcap-btc",0, true)));
                std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<'\n';
                arb.change_rate("mcap-btc", gain_r(arb.ob_size("mcap-btc",0, false)));
                std::cout<<"mcap-btc rate = "<<arb("mcap-btc").rate<<'\n';*/
                sfo.wait();
                auto cycles = arb.find_cycles();
                std::cout << "Found " << cycles.size() << " cycles\n";
                //api.get_ob(arb.extract_unique_pairs(cycles), arb);

                for(auto cycle : cycles) {
                    std::cout << ">>> CYCLE: " << cycle2string(cycle) << " gain=" << gain(arb, cycle) << '\n';

                    //for(auto p_it = cycle.begin(); p_it != std::prev(cycle.end()); ++p_it) {
                    for(size_t i = 0; i < cycle.size(); ++i) {
                        auto p = *cycle.begin();
                        //arb.change_rate(p, gain_r(arb.ob_size(p, 0, true)));

                        for(auto nxt_p = std::next(cycle.begin(), 0); nxt_p != cycle.end(); ++nxt_p) {
                            for(size_t j = 0; j < arb(p).ob.size(); ++j) {
                                //auto sizes = arb.sizing_fixed(cycle, *nxt_p, arb.ob_size(*nxt_p, j), {{p, gain_r(arb.ob_size(p, 0, true))}});
                                cycleSize_t sizes = arb.sizing(cycle, *nxt_p, arb.ob_size(*nxt_p, j)); 
                                arb.minsize(cycle, sizes);
                                arb.align_sizes(cycle, sizes);
                                double profit = calc_profit(cycle, sizes);

                                if(profit > 0 && arb.is_aligned(cycle, sizes) && arb.is_minsized(sizes)) {
                                    std::cout << "locked " << p << ", @rate=" << arb(p).rate << ", maximizing " << *nxt_p << ", row=" << j << " ->" << arb.ob_size(*nxt_p, j) << " gain=" << gain(arb, cycle) << " real_gain=" << calc_gain(cycle, sizes) << "\n";
                                    std::cout << print_sizes(arb, cycle, sizes) << " profit=" << profit << '\n';
                                    if(balance.count(as_pair(cycle[0]).first))
                                        if(balance[as_pair(cycle[0]).first] >= sizes[cycle[0]].first) {
                                            cs[cycle2string(cycle)] = sizes;
                                            std::cout << "groups: " << groupping(cycle, sizes, {}) << '\n';
                                        }
                                }
                            }
                            // calc sizes for min input
                            //cycleSize_t sizes = arb.sizing(cycle, *nxt_p, arb.sums1());
                        }

                        //arb.change_rate(p, gain_r(arb.ob_size(p, 0, false)));
                        std::rotate(cycle.begin(), std::next(cycle.begin()), cycle.end());
                    }

                    /*std::cout<<"DOMS:\n";
                    for(auto p: cycle) {
                        for(size_t i = 0; i < arb(p).ob.size(); ++i) {
                            if(i == 0) std::cout<<p<<": "<<arb.ob(p, 0, true)<<" %in sz% "<<arb.ob_size(p, 0, true)<<"\n-----------------\n";
                            std::cout<<p<<": "<<arb.ob(p, i)<<" %in sz% "<<arb.ob_size(p, i)<<'\n';
                        }
                        std::cout<<'\n';
                    }*/
                    //break;
                    std::cout << '\n';
                }
                std::vector<std::pair<std::string, cycleSize_t>> vcs(cs.begin(), cs.end());
                std::sort(vcs.begin(), vcs.end(), [&](auto& a, auto& b) {
                    auto ca = string2cycle(a.first);
                    auto cb = string2cycle(b.first);
                    double p_a = calc_profit(ca, a.second);
                    double p_b = calc_profit(cb, b.second);
                    if(arb(*ca.rbegin()).c2 != "usd") p_a *= arb(arb(*ca.rbegin()).c2+"-usd").rate;
                    if(arb(*cb.rbegin()).c2 != "usd") p_b *= arb(arb(*cb.rbegin()).c2+"-usd").rate;

                    return p_a > p_b;
                });
                if(vcs.size()) {
                    std::cout<<"Profitable cycles and sizes:\n";
                    for(auto item: vcs) {
                        std::cout<<item.first<<" sz:"<<print_sizes(arb, string2cycle(item.first), item.second)<<" profit="<<calc_profit(string2cycle(item.first), item.second)<<'\n';
                        auto cycle = string2cycle(item.first);
                        auto sizes = item.second;
                        auto groups = groupping(cycle, sizes, balance);
                        for(auto g: groups) {
                            std::cout<<"Trading group "<<g<<'\n';
                            elle::With<Scope>() << [&](Scope& scope_trading) {
                                for(auto p: g) {
                                    scope_trading.run_background("trading "+p, [&, p]{ std::cout<<">> Trading "<<p<<" "<<sizes[p]<<'\n'; });
                                }
                                scope_trading.wait();
                            };

                        }
                        std::cout<<"cycle is traded\n";
                    }
                    /*std::cout<<"Trading first profitable cycle:\n";
                    auto cycle = string2cycle(vcs[0].first);
                    auto sizes = vcs[0].second;
                    auto groups = groupping(cycle, sizes, balance);
                    for(auto g: groups) {
                        std::cout<<"Trading group "<<g<<'\n';
                        elle::With<Scope>() << [&](Scope& scope_trading) {
                            for(auto p: g) {
                                scope_trading.run_background("trading "+p, [&]{ std::cout<<">> Trading "<<p<<" "<<sizes[p]<<'\n'; });
                            }
                            scope_trading.wait();
                        };

                    }*/
                }
                
                std::cout << "balance: " << balance << '\n';
                std::cout << "delta: " << delta(b0, balance) << '\n';
                //std::cout<<"balance:"<<api.get_balance()<<'\n'<<'\n';
                api.save_nonces();
                sleep(1s);
            }

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
