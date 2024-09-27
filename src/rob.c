/**
 * \file rob.c
 *
 * \brief Give, buy, etc.
 *
 * This file is called rob.c for historical reasons, and one day it'll
 * probably get folded into some other file.
 *
 *
 */

#include "copyrite.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "attrib.h"
#include "case.h"
#include "conf.h"
#include "dbdefs.h"
#include "externs.h"
#include "flags.h"
#include "game.h"
#include "lock.h"
#include "log.h"
#include "match.h"
#include "mushdb.h"
#include "parse.h"
#include "strutil.h"

static void do_give_to(dbref player, const char *arg, int silent,
                       NEW_PE_INFO *pe_info);

/** Set an object's money value, with limit-checking.
 * \param thing dbref of object.
 * \param amount amount to set object's pennies to.
 */
void
s_Pennies(dbref thing, int amount)
{
  if (amount < 0)
    amount = 0;
  else if (amount > MAX_PENNIES)
    amount = MAX_PENNIES;
  Pennies(thing) = amount;
}

/** the buy command
 * \param player the enactor/buyer
 * \param item the item to buy
 * \param from who to buy it from
 * \param price the price to pay for it, or -1 for any price
 * \param pe_info the pe_info to use for lock checks
 */
void
do_buy(dbref player, char *item, char *from, int price, NEW_PE_INFO *pe_info)
{
  dbref vendor;
  dbref failvendor = NOTHING;
  char prices[BUFFER_LEN];
  char *plus;
  char *cost;
  char finditem[BUFFER_LEN];
  char buycost[BUFFER_LEN];
  int boughtit;
  int len;
  char buff[BUFFER_LEN], *bp;
  char obuff[BUFFER_LEN];
  char *r[BUFFER_LEN / 2];
  char *c[BUFFER_LEN / 2];
  int affordable;
  int costcount, ci;
  int count, i;
  int low, high; /* lower bound, upper bound of cost */
  ATTR *a;
  PE_REGS *pe_regs;

  if (!GoodObject(Location(player)))
    return;

  vendor = Contents(Location(player));
  if (vendor == player)
    vendor = Next(player);

  if (from != NULL && *from) {
    switch (vendor = match_result(player, from, TYPE_PLAYER | TYPE_THING,
                                  MAT_NEAR_THINGS | MAT_ENGLISH | MAT_TYPE)) {
    case NOTHING:
      notify(player, T("Buy from whom?"));
      return;
    case AMBIGUOUS:
      notify(player, T("I don't know who you mean!"));
      return;
    }
    if (vendor == player) {
      notify(player, T("You can't buy from yourself!"));
      return;
    }
  } else if (vendor == NOTHING) {
    notify(player, T("There's nobody here to buy things from."));
    return;
  } else {
    from = NULL;
  }

  if (!item || !*item || !(item = trim_space_sep(item, ' '))) {
    notify(player, T("Buy what?"));
    return;
  }

  bp = finditem;
  safe_str(item, finditem, &bp);
  safe_chr(':', finditem, &bp);
  *bp = '\0';
  for (bp = strchr(finditem, ' '); bp; bp = strchr(bp, ' '))
    *bp = '_';

  len = strlen(finditem);

  /* Scan pricelists */
  boughtit = -1;
  affordable = 1;
  do {
    if (vendor == player) {
      continue; /* Can't buy from yourself. Only occurs with no "from <vendor>"
                   arg */
    }
    a = atr_get(vendor, "PRICELIST");
    if (!a) {
      continue;
    }
    count = list2arr(r, BUFFER_LEN / 2,
                     strupper_r(atr_value(a), prices, sizeof prices), ' ', 0);
    if (!count) {
      continue;
    }
    for (i = 0; i < count; i++) {
      if (!strncasecmp(finditem, r[i], len)) {
        /* Check cost */
        cost = r[i] + len;
        if (!*cost)
          continue;
        costcount = list2arr(c, BUFFER_LEN / 2, cost, ',', 0);
        for (ci = 0; ci < costcount; ci++) {
          cost = c[ci];
          /* Formats:
           * 10,2000+,10-100
           */
          if ((plus = strchr(cost, '-'))) {
            *(plus++) = '\0';
            if (!is_strict_integer(cost))
              continue;
            if (!is_strict_integer(plus))
              continue;
            low = parse_integer(cost);
            high = parse_integer(plus);
            if (price < 0) {
              boughtit = low;
            } else if (price >= low && price <= high) {
              boughtit = price;
            }
          } else if ((plus = strchr(cost, '+'))) {
            *plus = '\0';
            if (!is_strict_integer(cost))
              continue;
            low = parse_integer(cost);
            if (price < 0) {
              boughtit = low;
            } else if (price > low) {
              boughtit = price;
            }
          } else if (is_strict_integer(cost)) {
            low = parse_integer(cost);
            if (price < 0) {
              boughtit = low;
            } else if (low == price) {
              boughtit = price;
            }
          } else {
            continue;
          }
          if (boughtit >= 0) {
            /* No point checking the lock before this point, as
               we don't try and give them money if they aren't
               selling what we're buying */
            if (!eval_lock_with(player, vendor, Pay_Lock, pe_info)) {
              boughtit = 0;
              if (failvendor == NOTHING)
                failvendor = vendor;
              /* We don't run fail_lock here in case we end up successfully
                 buying from someone else. Only fail_lock() if the failure
                 stops us buying from anyone */
              continue;
            }
            if (!payfor(player, boughtit)) {
              affordable = 0;
              boughtit = 0;
              continue;
            }
            giveto(vendor, boughtit);
            bp = strchr(finditem, ':');
            if (bp)
              *bp = '\0';
            for (bp = finditem; *bp; bp++)
              *bp = DOWNCASE(*bp);
            bp = buff;
            safe_format(buff, &bp, T("You buy a %s from %s."), finditem,
                        AName(vendor, AN_MOVE, NULL));
            *bp = '\0';
            bp = obuff;
            safe_format(obuff, &bp, T("buys a %s from %s."), finditem,
                        AName(vendor, AN_MOVE, NULL));
            *bp = '\0';
            bp = buycost;
            safe_integer(boughtit, buycost, &bp);
            *bp = '\0';
            pe_regs = pe_regs_create(PE_REGS_ARG, "do_buy");
            pe_regs_setenv_nocopy(pe_regs, 0, finditem);
            pe_regs_setenv_nocopy(pe_regs, 1, buycost);
            real_did_it(player, vendor, "BUY", buff, "OBUY", obuff, "ABUY",
                        NOTHING, pe_regs, NA_INTER_SEE, AN_MOVE);
            pe_regs_free(pe_regs);
            return;
          }
        }
      }
    }
  } while (!from && ((vendor = Next(vendor)) != NOTHING));

  if (failvendor != NOTHING) {
    /* Found someone selling, but they wouldn't take our money */
    char msg[BUFFER_LEN];
    snprintf(msg, sizeof msg, T("%s doesn't want your money."),
             AName(failvendor, AN_SYS, NULL));
    fail_lock(player, failvendor, Pay_Lock, msg, NOTHING);
  } else if (price >= 0) {
    /* Noone we wanted to buy from selling for the right amount */
    if (!from) {
      notify(player, T("I can't find that item with that price here."));
    } else {
      notify_format(player, T("%s isn't selling that item for that price"),
                    AName(vendor, AN_SYS, NULL));
    }
  } else if (affordable) {
    /* Didn't find anyone selling it */
    if (!from) {
      notify(player, T("I can't find that item here."));
    } else {
      notify_format(player, T("%s isn't selling that item."),
                    AName(vendor, AN_SYS, NULL));
    }
  } else {
    /* We found someone selling, but didn't have the pennies to buy it */
    notify(player, T("You can't afford that."));
  }
}

/** The give command.
 * \param player the enactor/giver.
 * \param recipient name of object to receive.
 * \param amnt name of object to be transferred, or amount of pennies.
 * \param silent if 1, hush the usual messages.
 * \param pe_info the pe_info to use for lock checks
 */
void
do_give(dbref player, const char *recipient, const char *amnt, int silent,
        NEW_PE_INFO *pe_info)
{
  dbref who;
  int amount;
  char tbuf1[BUFFER_LEN];
  char *bp;

  /* If we have a recipient, but no amnt, try parsing for
   * 'give <amnt> to <recipient>' instead of 'give <recipient>=<amount>'
   */
  if (recipient && *recipient && (!amnt || !*amnt)) {
    do_give_to(player, recipient, silent, pe_info);
    return;
  }

  /* check recipient */
  switch (who = match_result(player, recipient, TYPE_PLAYER,
                             MAT_NEAR_THINGS | MAT_ENGLISH)) {
  case NOTHING:
    notify(player, T("Give to whom?"));
    return;
  case AMBIGUOUS:
    notify(player, T("I don't know who you mean!"));
    return;
  }

  /* Can't give to garbage... */
  if (IsGarbage(who)) {
    notify(player, T("Give to whom?"));
    return;
  }

  if (!is_strict_integer(amnt)) {
    /* We're giving an object */
    dbref thing;
    switch (thing = match_result(player, amnt, TYPE_THING,
                                 MAT_POSSESSION | MAT_ENGLISH)) {
    case NOTHING:
      notify(player, T("You don't have that!"));
      return;
    case AMBIGUOUS:
      notify(player, T("I don't know which you mean!"));
      return;
    default:
      /* if you can give yourself, that's like "enter". since we
       * do no lock check with give, we shouldn't be able to
       * do this.
       */
      if (thing == player) {
        notify(player, T("You can't give yourself away!"));
        return;
      }
      /* Don't give things to themselves. */
      if (thing == who) {
        notify(player, T("You can't give an object to itself!"));
        return;
      }
      if (!eval_lock_with(player, thing, Give_Lock, pe_info)) {
        fail_lock(player, thing, Give_Lock, T("You can't give that away."),
                  NOTHING);
        return;
      }

      if (!eval_lock_with(player, who, From_Lock, pe_info)) {
        notify_format(player, T("%s doesn't want anything from you."),
                      AName(who, AN_SYS, NULL));
        return;
      }

      if (!eval_lock_with(thing, who, Receive_Lock, pe_info)) {
        notify_format(player, T("%s doesn't want that."),
                      AName(who, AN_SYS, NULL));
        return;
      }

      if (Mobile(thing) && (EnterOk(who) || controls(player, who))) {
        char recipient2[BUFFER_LEN];
        char gift[BUFFER_LEN];
        char giver[BUFFER_LEN];
        moveto(thing, who, player, "give");

        strcpy(recipient2, AName(who, AN_MOVE, NULL));
        strcpy(gift, AName(thing, AN_MOVE, NULL));
        strcpy(giver, AName(player, AN_MOVE, NULL));

        /* Notify the giver with their GIVE message */
        bp = tbuf1;
        safe_format(tbuf1, &bp, T("You gave %s to %s."), gift, recipient2);
        *bp = '\0';
        did_it_with(player, player, "GIVE", tbuf1, "OGIVE", NULL, "AGIVE",
                    NOTHING, thing, who, NA_INTER_SEE, AN_MOVE);

        /* Notify the object that it's been given */
        notify_format(thing, T("%s gave you to %s."), giver, recipient2);

        /* Recipient gets success message on thing and receive on self */
        did_it(who, thing, "SUCCESS", NULL, "OSUCCESS", NULL, "ASUCCESS",
               NOTHING, AN_SYS);
        bp = tbuf1;
        safe_format(tbuf1, &bp, T("%s gave you %s."), giver, gift);
        *bp = '\0';
        did_it_with(who, who, "RECEIVE", tbuf1, "ORECEIVE", NULL, "ARECEIVE",
                    NOTHING, thing, player, NA_INTER_SEE, AN_SYS);
      } else
        notify(player, T("Permission denied."));
    }
    return;
  }
  /* At this point, we're giving an amount. */
  amount = parse_integer(amnt);
  if (Pennies(who) >= Max_Pennies(who) && amount > 0) {
    notify_format(player, T("%s is rich enough already."),
                  AName(who, AN_SYS, NULL));
    return;
  }
  if (Pennies(who) + amount > Max_Pennies(who))
    amount = Max_Pennies(who) - Pennies(who);
  if (amount < 0 && !Can_Debit(player)) {
    notify(player, T("What is this, a holdup?"));
    return;
  } else if (amount == 0) {
    notify_format(player, T("You must specify a positive number of %s."),
                  MONIES);
    return;
  }
  if (Can_Debit(player) && (amount < 0) && (Pennies(who) + amount < 0)) {
    amount = -Pennies(who);
    if (amount == 0) {
      notify_format(player, T("%s have nothing left for you to take!"),
                    AName(who, AN_SYS, NULL));
      return;
    }
  }
  /* try to do the give */
  if (!Moneybags(player) && !payfor(player, amount)) {
    notify_format(player, T("You don't have that many %s to give!"), MONIES);
  } else {
    char paid[SBUF_LEN], *pb;
    bool has_cost;
    ufun_attrib ufun;

    has_cost =
      fetch_ufun_attrib("COST", who, &ufun,
                        UFUN_LOCALIZE | UFUN_REQUIRE_ATTR | UFUN_IGNORE_PERMS);
    if (!has_cost && !IsPlayer(who)) {
      notify_format(player, T("%s refuses your money."),
                    AName(who, AN_SYS, NULL));
      giveto(player, amount);
      return;
    } else if (has_cost && (amount > 0 || !IsPlayer(who))) {
      /* give pennies to object with COST */
      int cost = 0;
      char fbuff[BUFFER_LEN];
      PE_REGS *pe_regs = pe_regs_create(PE_REGS_ARG, "do_give");

      pb = paid;
      safe_integer_sbuf(amount, paid, &pb);
      *pb = '\0';
      pe_regs_setenv_nocopy(pe_regs, 0, paid);
      call_ufun(&ufun, fbuff, player, player, pe_info, pe_regs);
      if (amount < (cost = atoi(fbuff))) {
        notify(player, T("Feeling poor today?"));
        giveto(player, amount);
        pe_regs_free(pe_regs);
        return;
      }
      if (cost < 0) {
        notify_format(player, T("%s refuses your money."),
                      AName(who, AN_SYS, NULL));
        giveto(player, amount);
        pe_regs_free(pe_regs);
        return;
      }
      if (!eval_lock_with(player, who, Pay_Lock, pe_info)) {
        char msg[BUFFER_LEN];
        giveto(player, amount);
        snprintf(msg, sizeof msg, T("%s refuses your money."),
                 AName(who, AN_SYS, NULL));
        fail_lock(player, who, Pay_Lock, msg, NOTHING);
        pe_regs_free(pe_regs);
        return;
      }
      if ((amount - cost) > 0) {
        notify_format(player, T("You get %d in change."), amount - cost);
      } else {
        notify_format(player, T("You paid %d %s."), amount,
                      ((amount == 1) ? MONEY : MONIES));
      }
      giveto(player, amount - cost);
      giveto(who, cost);
      real_did_it(player, who, "PAYMENT", NULL, "OPAYMENT", NULL, "APAYMENT",
                  NOTHING, pe_regs, NA_INTER_SEE, AN_SYS);
      pe_regs_free(pe_regs);
      return;
    } else {
      PE_REGS *pe_regs;
      /* give pennies to a player with no @cost, or "give" a negative amount to
       * a player */
      if (!Wizard(player) && !eval_lock_with(player, who, Pay_Lock, pe_info)) {
        char msg[BUFFER_LEN];
        giveto(player, amount);
        snprintf(msg, sizeof msg, T("%s refuses your money."),
                 AName(who, AN_SYS, NULL));
        fail_lock(player, who, Pay_Lock, msg, NOTHING);
        return;
      }
      if (amount > 0) {
        notify_format(player, T("You give %d %s to %s."), amount,
                      ((amount == 1) ? MONEY : MONIES),
                      AName(who, AN_MOVE, NULL));
      } else {
        notify_format(player, T("You took %d %s from %s!"), abs(amount),
                      ((abs(amount) == 1) ? MONEY : MONIES),
                      AName(who, AN_MOVE, NULL));
      }
      if (IsPlayer(who) && !silent) {
        if (amount > 0) {
          notify_format(who, T("%s gives you %d %s."),
                        AName(player, AN_MOVE, NULL), amount,
                        ((amount == 1) ? MONEY : MONIES));
        } else {
          notify_format(who, T("%s took %d %s from you!"),
                        AName(player, AN_MOVE, NULL), abs(amount),
                        ((abs(amount) == 1) ? MONEY : MONIES));
        }
      }
      giveto(who, amount);
      pb = paid;
      safe_integer_sbuf(amount, paid, &pb);
      *pb = '\0';
      pe_regs = pe_regs_create(PE_REGS_ARG, "do_give");
      pe_regs_setenv_nocopy(pe_regs, 0, paid);
      real_did_it(player, who, "PAYMENT", NULL, "OPAYMENT", NULL, "APAYMENT",
                  NOTHING, pe_regs, NA_INTER_SEE, AN_SYS);
      pe_regs_free(pe_regs);
    }
  }
}

/** The other syntax of the give command.
 * \param player the enactor/giver.
 * \param arg "something to someone".
 * \param silent if 1, hush the usual messages.
 */
static void
do_give_to(dbref player, const char *arg, int silent, NEW_PE_INFO *pe_info)
{
  char *s;
  char buff[BUFFER_LEN];

  /* Parse out the object and recipient */
  arg = strupper_r(arg, buff, sizeof buff);
  s = (char *) string_match(arg, "TO ");
  if (!s) {
    notify(player, T("Did you want to give something *to* someone?"));
    return;
  }
  while ((s > arg) && isspace(*(s - 1))) {
    s--;
  }
  if (s == arg) {
    notify(player, T("Give what?"));
    return;
  }
  *s++ = '\0';
  s = (char *) string_match(s, "TO ");
  s += 3;
  while (*s && isspace(*s))
    s++;
  if (!*s) {
    notify(player, T("Give to whom?"));
    return;
  }
  /* At this point, 'arg' is the object, and 's' is the recipient.
   * But be double-safe to be sure we don't loop.
   */
  if (!*arg || !*s) {
    notify(player, T("I don't know what you mean."));
    return;
  }
  do_give(player, s, arg, silent, pe_info);
  return;
}
