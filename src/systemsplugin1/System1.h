/*
Copyright (C) 2018 Adrian Michel
http://www.amichel.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * IMPORTANT DISCLAIMER
 *
 * These sample trading systems are presented for illustrative purpose only and
 * are not intended for use in real trading.
 *
 */

#pragma once

/**
 * System1 illustrates some of the techniques that are emplyed in writing
 * trading systems for the SimLib framework
 *
 * It is not intended as a system to be used in real trading, and most likely,
 * it is not profitable
 *
 * System11 is derived from
 * - BarSystem, indicating that this is a system that expects bar data
 * - OpenPOsotionHandler, which enables it to receive notifications for each
 * open position when looping through a collection of open positions, by calling
 * the method forEachOpenPosition for example
 * - BarHandler, which allows it to receive notifications on each bar of data
 * when looping methods such as forEachBar are called
 * - OrderFilter, which allows it to receive notifications before each order
 * method is executed
 *
 * Each of these base classes defines one more more pure virtual methods that
 * have to be implemented in System1 and which will receive the respective
 * notifications.
 *
 *
 * @see BarSystem
 * @see OpenPositionsHandler
 * @see BarHandler
 * @see OrderFilter
 */
class System1 : public BarSystem<System1> {
 private:
  // time based holding period, indicates how many bars a position will be held
  // - it will be used with installTimeBasedExit
  const unsigned int _bars;
  // profit target - indicates what level of profit needs to be reached before
  // closing a position - will be used with installProfitTarget
  const double _profitTarget;
  // this is pointer to a user defined series. The user still needs to create it
  // in init or elsewhere by calling Series::create( size )
  // The user doesn't need to delete or deallocate this or any other series -
  // the framework takes care of it
  Series series;
  // series containing the simple moving average, will be initialized in the
  // init method
  Series sma;

 private:
  // this method is declared in OpenPositionHandler class.
  // it will be called for each  open position when the method
  // forEachOpenPosition is called further down in this case it doesn't do
  // anything, but positions can be closed for example, or used in any other way
  // in the logic of the system
  virtual bool onOpenPosition(tradery::Position pos, Index bar) {
    // do something here with the position
    return true;
  }

 public:
  //  constructor of the System1 class
  // does one time initialization of the class, which whill hold for the life of
  // the class, indicated by the "const" qualifier associated with the
  // variables. Per run intialization should be done in the method "init".
  System1(const std::vector<std::string>* params = 0)
      // set the system name and the holding period to 2 days, profit target to
      // 5%
      : BarSystem<System1>(Info("054A4BB5-54D7-4c98-ABCA-AC581726061E",
                                "System 1 - basic techniques",
                                "This system illustrates a few basic "
                                "techniques for writing a trading sytem class "
                                "using the SimLib Framework")),
        _bars(2),
        _profitTarget(5) {}

  // this is called by the framework before running the system.
  // used to do initializations for each run on a symbol
  virtual bool init(const std::string& symbol) {
    PRINT_LINE(red << name() << " in init, on " << getSymbol());

    // registers itself as an order filter with the default positions
    // an order filter could have been implemented as a separate class,
    // which would have allowed it to be used by other systems as well
    // onBuyAtLimit will be called for every buyAtLimit call
    positions().registerOrderFilter(this);

    // calculates the 10 bars SMA series. this will be calculated on each run
    sma = volumeSeries().SMA(10);

    // create two temporary series
    Series tr(DEF_BARS.size());
    Series atr(DEF_BARS.size());

    // the true range series are not useful in this case,
    // they are just calculated to show it can be done, and how to
    // calculate the true range series values
    unsigned int bar = 1;
    for (; bar < DEF_BARS.size(); bar++) {
      double h = high(bar);
      double l = low(bar);
      double c = close(bar);
      double pc = close(bar - 1);

      tr[bar] = max3<double>(h - l, pc - h < 0 ? h - pc : pc - h,
                             pc - l < 0 ? l - pc : pc - l) /
                c;
    }

    // create a new series trsma - 3 bars SMA of the true range
    Series trsma = tr.SMA(3);

    // calculates the average true range
    atr[3] = trsma[3];
    for (bar = 3; bar < DEF_BARS.size(); bar++) {
      double x = tr[bar];
      double prev = atr[bar - 1];
      atr[bar] = (prev * (3 - 1) + x) / 3;
    }

    // get the EMA of the close with period 4 bars
    // the series of limit prices will be the close_ema * 0.95
    Series close_ema = closeSeries().EMA(4);

    // calculate a new series - each value is 95% of the close_ema calculated
    // above using overloaded operator *
    series = 0.95 * close_ema;

    // install the exit condtions:
    // 5% profit target
    INSTALL_PROFIT_TARGET(_profitTarget);
    INSTALL_TIME_BASED_EXIT(_bars);

    // signal that init was performed succesfully
    // a false return would signal some error and would stop the run
    return true;
  }

  // the run method - this is called by the framework after a succesful
  // initialization it just does forEachBar which in turn calls dataHandler for
  // all the available bars
  virtual void run() { FOR_EACH_BAR(10); }

  // this is the buy at limit dataHandler - it is called before buy at limit is
  // submitted and gives the system a last chance to change it or even not
  // submit it at all
  // the return will indicate how many shares this order should be actually
  // placed for. If 0, no order will be submitted
  unsigned int onBuyAtLimit(size_t bar, unsigned int shares,
                            double price) const {
    unsigned int sh;
    //		outputSink() << OutputSink::Color::red << name() <<
    // OutputSink::Control::reset  << " in onBuyAtLimit" <<
    // OutputSink::Control::endl;
    // in this case we are just doing some money maanagemt as well as some
    // filtering based on previous close prices

    // if it closed lower the previous two bars
    // and the total value traded was higher than $1.2mil
    // and the price of the order is lower than the last close
    // then buy at limit the number of shares calculated by getShares

    // IMPORTANT: since buyAtLimit is called on bar + 1 (see below), we have to
    // look at the last bar prior to that, so we use bar - 1, otherwise this
    // will trigger a bar out of range error for the last bar available so
    // instead bar, we use bar - 1 (which is the last bar before the bar on
    // which the buy would happen) etc.
    if ((close(bar - 1) < close(bar - 2)) &&
        (close(bar - 2) < close(bar - 3)) && (price * sma[bar - 1] > 1200000) &&
        (price < close(bar - 1)))
      sh = getShares(bar - 1, price);
    else
      // otherwise don't place the order
      sh = 0;
    return sh;
  }

  // this implements a simple money management scheme and calculates the number
  // of shares by making sure the the position value is alwyas $5000
  unsigned int getShares(size_t bar, double price) const {
    return (unsigned int)(5000 / price);
  }

  // called for each bar
  virtual void onBar(Index bar) {
    // apply auto stops - so check for profit target and holding period in our
    // case
    APPLY_AUTO_STOPS(bar);
    // get the limit price for the current bar
    double buyLimitPrice = series[bar];

    //    obx << getDefBars()->getTime( bar ).to_simple_string() << "-" <<
    //    buyLimitPrice << ", ";
    // buy at limit on the next bar (it should always be next bar or the system
    // would be peeking into the future!) use a default value of 1000 shares -
    // it will be re-calculated by onBuyAtLimit
    BUY_AT_LIMIT(bar + 1, buyLimitPrice, 1000, "buy at limit");

    //    outputSink().printLine( obx );

    // call this method just to show how it's done - the onOpenPosition will be
    // called for each open position
    FOR_EACH_OPEN_POSITION(bar);
  }
};

class System20 : public BarSystem<System20> {
  Series ratioPrice;
  Series ratioVolume;
  int longMA;
  int shortMA;

 public:
  System20(const std::vector<std::string>* params = 0)
      // set the system name and the holding period to 2 days, profit target to
      // 5%
      : BarSystem<System20>(Info("054A4BB5-54D7-4c98-ABCA-AC581726061E",
                                 "System 1 - basic techniques",
                                 "This system illustrates a few basic "
                                 "techniques for writing a trading sytem class "
                                 "using the SimLib Framework"))

  {}

  bool init() {
    // each system receives a symbol iterator, which contains all the
    // symbols for the session.

    if (defSymbol() != getFirstSymbol()) return false;

    for (String otherSymbol = getNextSymbol(); !otherSymbol.empty();
         otherSymbol = getNextSymbol()) {
      Bars otherSymbolData = bars(otherSymbol);

      if (otherSymbolData) {
        // if data available
        // create a data synchronizer for the current symbol
        // with the reference being the current symbol
        otherSymbolData.synchronize(DEF_BARS);

        // create a series synchronizer for the sma 5
        // of the closing prices of the first symbol
        Series ss = otherSymbolData.closeSeries().SMA(5);

        // add the current close series to the
        // synchronized first symbol close series
        Series sum = CLOSE_SERIES + ss;

        // print to the output window some of the values
        // of the two close prices and the sum
        for (size_t n = 0; n < 20; n++)
          PRINT_LINE(time(n).to_simple_string()
                     << ", " << close(n) << ", " << ss[n] << ", " << sum[n]);
      }
    }
    return true;

    longMA = 40;
    shortMA = 3;

    ratioPrice = HIGH_SERIES.SMA(shortMA) / HIGH_SERIES.SMA(longMA);
    ratioVolume = VOLUME_SERIES.SMA(shortMA) / VOLUME_SERIES.SMA(longMA);

    INSTALL_TIME_BASED_EXIT(10);
    INSTALL_PROFIT_TARGET(5);
    return true;
  }

  bool signal(Index bar) {
    return ratioPrice[bar - 1] > 2 AND ratioVolume[bar - 1] >
           5 AND volume(bar)<volume(bar - 1) AND volume(bar - 1)> volume(
               bar - 2) AND close(bar)<close(bar - 1) AND open(bar - 1)>
               close(bar - 2);
  }

  void run() {
    //  this calls onBar for each available bar starting at 10
    // replace 10 with the number of bars the system
    // should wait before starting the processing
    FOR_EACH_BAR(longMA);
  }

  // bar is the current bar which is being processed
  void onBar(Index bar) {
    // apply the auto exit strategies
    APPLY_AUTO_STOPS(bar);

    if (signal(bar) AND OPEN_POSITIONS_COUNT == 0 AND close(bar) > 5)
      SHORT_AT_CLOSE(bar, 1000, "Shorting at close");
  }
};
