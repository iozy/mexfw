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
#include <elle/reactor/fsm.hh>
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
            Signal sbu, sbc, sfo;
            //std::cout<<"pairs="<<api.get_all_pairs()<<'\n';
            //std::cout<<"opened orders: "<<api.get_open_orders()<<'\n';
            /*for(auto b: EXCHANGE::bases) {
                std:: cout<<b<<" ";
            }
            std::cout<<'\n';*/
            scope.run_background("update_balance", [&] {
                std::unordered_map<std::string, double> old_balance;
                while(!sched.done()) {
                    api.update_balance(balance);
                    sbu.signal();
                    if(balance != old_balance) {
                        sbc.signal();
                        std::cout<<"balance changed\n";
                    }
                    old_balance = balance;
                    sleep(1s);
                }
            });
            scope.run_background("get_full_ob", [&] {
                while(!sched.done()) {
                    api.get_full_ob(arb);
                    sfo.signal();
                    sleep(1s);
                }
            });
            scope.run_background("get_init_balance", [&] {
                sbu.wait();
                b0 = balance;
            });

            while(true) {
                // vcs[cycle_str] = sizes
                std::vector<std::pair<std::string, cycleSize_t>> vcs;
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
                    //std::cout << ">>> CYCLE: " << cycle2string(cycle) << " gain=" << gain(arb, cycle) << '\n';

                    //for(auto p_it = cycle.begin(); p_it != std::prev(cycle.end()); ++p_it) {
                    for(size_t i = 0; i < cycle.size(); ++i) {
                        auto p = *cycle.begin();
                        //arb.change_rate(p, gain_r(arb.ob_size(p, 0, true)));

                        for(auto nxt_p = std::next(cycle.begin(), 0); nxt_p != cycle.end(); ++nxt_p) {
                            //auto sizes = arb.sizing_fixed(cycle, *nxt_p, arb.ob_size(*nxt_p, j), {{p, gain_r(arb.ob_size(p, 0, true))}});
                            cycleSize_t sizes = arb.sizing(cycle, *nxt_p, arb.sums2(*nxt_p, 0.0005));
                            arb.minsize(cycle, sizes);
                            arb.align_sizes(cycle, sizes);
                            double profit = calc_profit(cycle, sizes);

                            if(profit > 0 && arb.is_aligned(cycle, sizes) && arb.is_minsized(sizes)) {
                                //std::cout << "locked " << p << ", @rate=" << arb(p).rate << ", maximizing " << *nxt_p << ", row=" << j << " ->" << arb.ob_size(*nxt_p, j) << " gain=" << gain(arb, cycle) << " real_gain=" << calc_gain(cycle, sizes) << "\n";
                                //std::cout << print_sizes(arb, cycle, sizes) << " profit=" << profit << '\n';

                                if(balance.count(as_pair(cycle[0]).first))
                                    if(balance[as_pair(cycle[0]).first] >= sizes[cycle[0]].first) {
                                        vcs.push_back({cycle2string(cycle), sizes});
                                    }
                            }

                            for(size_t j = 0; j < arb(p).ob.size(); ++j) {
                                sizes = arb.sizing(cycle, *nxt_p, arb.ob_size(*nxt_p, j));
                                arb.minsize(cycle, sizes);
                                arb.align_sizes(cycle, sizes);
                                profit = calc_profit(cycle, sizes);

                                if(profit > 0 && arb.is_aligned(cycle, sizes) && arb.is_minsized(sizes)) {
                                    //std::cout << "locked " << p << ", @rate=" << arb(p).rate << ", maximizing " << *nxt_p << ", row=" << j << " ->" << arb.ob_size(*nxt_p, j) << " gain=" << gain(arb, cycle) << " real_gain=" << calc_gain(cycle, sizes) << "\n";
                                    //std::cout << print_sizes(arb, cycle, sizes) << " profit=" << profit << '\n';

                                    if(balance.count(as_pair(cycle[0]).first))
                                        if(balance[as_pair(cycle[0]).first] >= sizes[cycle[0]].first) {
                                            vcs.push_back({cycle2string(cycle), sizes});
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
                    //std::cout << '\n';
                }

                std::sort(vcs.begin(), vcs.end(), [&](auto & a, auto & b) {
                    auto ca = string2cycle(a.first);
                    auto cb = string2cycle(b.first);
                    double p_a = calc_profit(ca, a.second);
                    double p_b = calc_profit(cb, b.second);

                    if(arb(*ca.rbegin()).c2 != "usd") p_a *= arb(arb(*ca.rbegin()).c2 + "-usd").rate;

                    if(arb(*cb.rbegin()).c2 != "usd") p_b *= arb(arb(*cb.rbegin()).c2 + "-usd").rate;

                    return p_a > p_b;
                });

                if(vcs.size()) {
                    std::cout << "Profitable cycles and sizes:\n";

                    //for(auto item : vcs) {
                    {
                        auto item = vcs[0];
                        std::cout << item.first << "\nsz:" << print_sizes(arb, string2cycle(item.first), item.second) << " profit=" << calc_profit(string2cycle(item.first), item.second) << '\n';
                        auto cycle = string2cycle(item.first);
                        auto sizes = item.second;
                        auto groups = groupping(cycle, sizes, balance);

                        for(auto g : groups) {
                            fsm::Machine m;
                            // signals: timeout, go_next, signal not enough
                            Signal sto, go_nxt, sne;
                            bool failed = true;
                            auto gb0 = balance;
                            auto& trading_state = m.state_make("trading", [&, g]{
                                std::cout<<"trading "<<g<<'\n';
                                elle::With<Scope>() << [&, g](Scope& s) {
                                    for(auto p: g)
                                        s.run_background("trading "+p, [&, p] {
                                            std::cout<<"placing order "<<api.tradesz(arb, p, sizes[p])<<'\n';
                                        });                                    
                                    if(s.wait(30s)) {
                                        s.run_background("balance_checking", [&]{
                                            while(true) {
                                                sbc.wait(3s);
                                                auto d = delta(gb0, balance);

                                                // get last group and last coin in last pair
                                                auto last_coin = as_pair(*g.rbegin()).second;
                                                std::cout<<"d="<<d<<" c="<<last_coin<<" sizes["<<*g.rbegin()<<"]="<<sizes[*g.rbegin()]<<'\n';
                                                if(d[last_coin] >= sizes[*g.rbegin()].second) {
                                                    std::cout<<"got enough!\n";
                                                    go_nxt.signal();
                                                    break;
                                                }
                                                else {
                                                    std::cout<<"not enough(((\n";
                                                    //sne.signal();
                                                }
                                            }
                                        });
                                        if(!s.wait(30s)) {
                                            std::cout<<"balance completion timeout\n";
                                            sto.signal();
                                        }
                                    } else {
                                        std::cout<<"placing order timeout\n";
                                        sto.signal();
                                    }
                                };

                            });
                            auto& failed_state = m.state_make("failed", [g]{ std::cout<<"failed "<<g<<'\n'; });
                            auto& done_state = m.state_make("done", [&, g]{
                                std::cout<<"done "<<g<<'\n'; failed = false;
                            });
                            m.transition_add(trading_state, done_state, Waitables{&go_nxt}).action([]{std::cout<<"group done\n";});
                            m.transition_add(trading_state, failed_state, Waitables{&sto}).action([]{std::cout<<"failed\n";});
                            m.run();
                            if(failed) break;	
                        }

                        std::cout << "cycle is traded\n";
                    }

                }

                std::cout << "balance: " << balance << '\n';
                std::cout << "delta: " << delta(b0, balance) << '\n'<< '\n';
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
