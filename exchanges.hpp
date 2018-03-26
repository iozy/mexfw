/*
 * All exchanges and their parameters
 */

#ifndef EXCHANGES_H
#define EXCHANGES_H
#include <initializer_list>

namespace mexfw {

/***********************************************************/
struct CCEX {
    /*c-cex.com*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"btc", "doge", "ltc", "usd"}; 
    static constexpr auto base_rates = {"btc-usd", "doge-btc", "ltc-btc", "ltc-usd", "doge-ltc", "doge-usd"};
    static constexpr auto rev_base_rates = {"usd-btc", "btc-doge", "btc-ltc", "usd-ltc", "ltc-doge", "usd-doge"};
    static constexpr auto delimeter = "-";
};
constexpr decltype(CCEX::bases) CCEX::bases;
constexpr decltype(CCEX::base_rates) CCEX::base_rates;
constexpr decltype(CCEX::rev_base_rates) CCEX::rev_base_rates;
/***********************************************************/
struct CEX {
    /*cex.io*/
    static constexpr auto fee = 0.0025;
    static constexpr auto bases = {"USD", "EUR", "GBP", "RUB", "BTC"};
    static constexpr auto delimeter = ":";
};
constexpr decltype(CEX::bases) CEX::bases;
/***********************************************************/
struct YOBIT {
    /*yobit.net*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"btc", "doge", "usd", "rur", "eth", "waves"};
    static constexpr auto delimeter = "_";
};
/***********************************************************/
struct CRYPTOPIA {
    /*cryptopia.co.nz*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"BTC", "DOGE", "USDT", "NZDT", "LTC"};
    static constexpr auto delimeter = "/";
};
/***********************************************************/
struct HITBTC {
    /*hitbtc.com*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"BTC", "ETH", "USD"};
    static constexpr auto delimeter = "";
};
/***********************************************************/
struct EXMO {
    /*exmo.com*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"BTC", "ETH", "USD", "USDT", "RUB", "EUR", "UAH", "PLN", "LTC"};
    static constexpr auto delimeter = "_";
};
/***********************************************************/
struct BITFINEX {
    /*bitfinex.com*/
};
/***********************************************************/
struct LIVECOIN {
    /*livecoin.net*/
};
/***********************************************************/
}
#endif  /*EXCHANGES_H*/
