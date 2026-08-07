#include "leak_grade.h"

#include <string.h>

/* What each field is worth, out of 100.
 *
 * The weights are not arbitrary: they track how close the field gets an
 * attacker to spending your money or to knowing who you are. The number and
 * the expiry date together are what a card-not-present checkout asks for, so
 * between them they carry over half the score. The rest is fingerprinting —
 * real, but not directly monetisable. */
#define PTS_PAN 35
#define PTS_EXPIRY 20
#define PTS_NAME 20
#define PTS_LOG 10
#define PTS_MERCHANT 8
#define PTS_SERVICE 2
#define PTS_ATC 1
#define PTS_PIN_TRY 1
#define PTS_COUNTRY 1
#define PTS_CURRENCY 1
#define PTS_APP 1

static uint8_t field_points(LeakField field) {
    switch(field) {
    case LeakFieldPan:
        return PTS_PAN;
    case LeakFieldExpiry:
        return PTS_EXPIRY;
    case LeakFieldName:
        return PTS_NAME;
    case LeakFieldLog:
        return PTS_LOG;
    case LeakFieldMerchant:
        return PTS_MERCHANT;
    case LeakFieldServiceCode:
        return PTS_SERVICE;
    case LeakFieldAtc:
        return PTS_ATC;
    case LeakFieldPinTry:
        return PTS_PIN_TRY;
    case LeakFieldCountry:
        return PTS_COUNTRY;
    case LeakFieldCurrency:
        return PTS_CURRENCY;
    case LeakFieldApp:
        return PTS_APP;
    default:
        return 0;
    }
}

static LeakGrade grade_from_score(uint8_t score) {
    if(score == 0) return LeakGradeAPlus;
    if(score <= 15) return LeakGradeA;
    if(score <= 35) return LeakGradeB;
    if(score <= 55) return LeakGradeC;
    if(score <= 75) return LeakGradeD;
    return LeakGradeF;
}

void leak_grade(const EmvCard* card, LeakReport* out) {
    if(out == NULL) return;
    memset(out, 0, sizeof(*out));
    if(card == NULL) {
        out->grade = LeakGradeAPlus;
        return;
    }

    /* A PAN that fails its own check digit is not a card number — it is a
     * masked or placeholder value, and crediting it would overstate the leak. */
    out->leaked[LeakFieldPan] = card->has_pan && card->pan_luhn_ok;
    out->leaked[LeakFieldExpiry] = card->has_expiry;
    out->leaked[LeakFieldName] = card->has_name; /* placeholders do not count */
    out->leaked[LeakFieldLog] = card->log_num > 0;
    out->leaked[LeakFieldServiceCode] = card->has_service_code;
    out->leaked[LeakFieldAtc] = card->has_atc;
    out->leaked[LeakFieldPinTry] = card->has_pin_try;
    out->leaked[LeakFieldCountry] = card->has_country;
    out->leaked[LeakFieldCurrency] = card->has_currency;
    out->leaked[LeakFieldApp] = card->aid_len > 0 || card->has_label;

    for(uint8_t i = 0; i < card->log_num; i++) {
        if(card->log[i].has_merchant) {
            out->leaked[LeakFieldMerchant] = true;
            break;
        }
    }

    uint16_t score = 0;
    for(int f = 0; f < LeakFieldCount; f++) {
        if(!out->leaked[f]) continue;
        out->points[f] = field_points((LeakField)f);
        score += out->points[f];
        out->leaked_count++;
    }
    if(score > 100) score = 100;
    out->score = (uint8_t)score;

    out->cnp_capable = out->leaked[LeakFieldPan] && out->leaked[LeakFieldExpiry];

    out->grade = grade_from_score(out->score);

    /* Floor: once the number and the date are both in the clear, a stranger can
     * go and try to spend it. No amount of restraint elsewhere on the card
     * earns a passing mark after that, so the bands cannot report better than
     * a D. In practice the weights already land there — this makes it a rule
     * rather than a coincidence, and the tests hold it to that. */
    if(out->cnp_capable && out->grade < LeakGradeD) out->grade = LeakGradeD;
}

/* ------------------------------------------------------------- strings */

const char* leak_grade_name(LeakGrade grade) {
    switch(grade) {
    case LeakGradeAPlus:
        return "A+";
    case LeakGradeA:
        return "A";
    case LeakGradeB:
        return "B";
    case LeakGradeC:
        return "C";
    case LeakGradeD:
        return "D";
    default:
        return "F";
    }
}

const char* leak_grade_verdict(LeakGrade grade) {
    switch(grade) {
    case LeakGradeAPlus:
        return "Nothing readable. Your card refused every question.";
    case LeakGradeA:
        return "Only fingerprint data. The account number stayed private.";
    case LeakGradeB:
        return "Some detail leaked, but not the account number.";
    case LeakGradeC:
        return "Real data left your card without your consent.";
    case LeakGradeD:
        return "Number and expiry, in the clear. Enough to try a purchase.";
    default:
        return "Number, expiry and more. A stranger now knows who you are.";
    }
}

const char* leak_field_name(LeakField field) {
    switch(field) {
    case LeakFieldPan:
        return "Card number";
    case LeakFieldExpiry:
        return "Expiry date";
    case LeakFieldName:
        return "Your name";
    case LeakFieldLog:
        return "Spending history";
    case LeakFieldMerchant:
        return "Where you shopped";
    case LeakFieldServiceCode:
        return "Service code";
    case LeakFieldAtc:
        return "Tap counter";
    case LeakFieldPinTry:
        return "PIN tries left";
    case LeakFieldCountry:
        return "Issuing country";
    case LeakFieldCurrency:
        return "Currency";
    case LeakFieldApp:
        return "Card product";
    default:
        return "?";
    }
}

const char* leak_field_what(LeakField field) {
    switch(field) {
    case LeakFieldPan:
        return "The long number printed on the front. The chip hands it to any "
               "reader that asks, in plain text, with no PIN, no confirmation "
               "and no record that it happened.";
    case LeakFieldExpiry:
        return "The month and year printed under the number. It travels in the "
               "same field as the number itself, so it is never a separate "
               "thing to steal.";
    case LeakFieldName:
        return "The cardholder name. Many issuers now send the word UNKNOWN "
               "here instead of a person. Yours sent the person.";
    case LeakFieldLog:
        return "Some cards keep the last ten to fifteen transactions on the "
               "chip: the date, the amount and the currency of each one.";
    case LeakFieldMerchant:
        return "The merchant name and location stored with each logged "
               "transaction. Not just what you spent, but where you were.";
    case LeakFieldServiceCode:
        return "Three digits describing where the card may be used and whether "
               "a PIN is expected.";
    case LeakFieldAtc:
        return "A counter the card increases by one on every single tap it has "
               "ever performed.";
    case LeakFieldPinTry:
        return "How many wrong PIN attempts remain before the card locks "
               "itself.";
    case LeakFieldCountry:
        return "The country whose bank issued the card.";
    case LeakFieldCurrency:
        return "The currency the account is held in.";
    default:
        return "The application identifier and label — which scheme the card "
               "belongs to and what product it is.";
    }
}

const char* leak_field_risk(LeakField field) {
    switch(field) {
    case LeakFieldPan:
        return "It is the account. Paired with the expiry date it is what an "
               "online checkout asks for, and plenty of merchants still do not "
               "require the three digits on the back.";
    case LeakFieldExpiry:
        return "It is the second of the two fields a checkout wants. Brute "
               "forcing it takes at most sixty guesses. Here it took none.";
    case LeakFieldName:
        return "It ties a number to a person. That is the difference between a "
               "stolen number and a convincing phone call from your bank, and "
               "it is what makes targeted phishing work.";
    case LeakFieldLog:
        return "A readable spending history, obtained in about a second, with "
               "no bank, no account access and no warrant.";
    case LeakFieldMerchant:
        return "It turns a list of amounts into a map of your week: your "
               "shops, your city, your routine, your habits.";
    case LeakFieldServiceCode:
        return "Small on its own. Historically it was copied onto forged "
               "magnetic stripes to tell a terminal to skip the chip.";
    case LeakFieldAtc:
        return "Read the card twice and the difference tells you how often it "
               "was used in between. A crude activity monitor on a person.";
    case LeakFieldPinTry:
        return "It reveals whether the card is close to locking. It is not a "
               "path to money.";
    case LeakFieldCountry:
        return "One more field narrowing who and where you are when combined "
               "with the rest.";
    case LeakFieldCurrency:
        return "Fingerprinting. On its own it identifies nobody.";
    default:
        return "Fingerprinting. It tells an attacker which fraud checks the "
               "card is likely to be behind.";
    }
}

const char* leak_field_defence(LeakField field) {
    switch(field) {
    case LeakFieldPan:
        return "Nothing on the card can be switched off. A shielded wallet "
               "stops the read outright. A phone or watch wallet sends a "
               "device number instead of this one, so the real number never "
               "goes on the air.";
    case LeakFieldExpiry:
        return "It changes only when the card is reissued. What actually stops "
               "the purchase is the fraud check and 3-D Secure at the "
               "merchant, not anything you control.";
    case LeakFieldName:
        return "This one is entirely the issuer's choice, and some issuers "
               "have already stopped. It is a reasonable thing to ask yours "
               "about.";
    case LeakFieldLog:
        return "The log is optional and issuers can turn it off. Until they "
               "do, shielding the card is what stops it being read.";
    case LeakFieldMerchant:
        return "Same as the log it belongs to: an issuer setting, not a "
               "cardholder one. Shielding blocks the read.";
    case LeakFieldServiceCode:
        return "Stripe fallback is being retired worldwide, and most terminals "
               "now refuse it.";
    case LeakFieldAtc:
        return "Leave it alone. This counter is a security feature: it is part "
               "of why replaying an old tap does not work.";
    case LeakFieldPinTry:
        return "It resets as soon as you use the correct PIN.";
    default:
        return "Not something to defend against on its own. It matters only "
               "next to the fields above it.";
    }
}

const char* leak_safe_name(LeakSafeFact fact) {
    switch(fact) {
    case LeakSafeCvv:
        return "The 3 digits (CVV)";
    case LeakSafePin:
        return "Your PIN";
    case LeakSafeReplay:
        return "A reusable payment";
    default:
        return "Your balance";
    }
}

const char* leak_safe_text(LeakSafeFact fact) {
    switch(fact) {
    case LeakSafeCvv:
        return "Not stored on the chip and not transmitted. No reader can "
               "obtain it, including this one. It is printed on the card and "
               "nowhere else, which is exactly why checkouts ask for it.";
    case LeakSafePin:
        return "Never leaves the chip. The card verifies it internally and "
               "answers only yes or no. There is no command that reads it out.";
    case LeakSafeReplay:
        return "Every tap is signed with a one-time cryptogram over a counter "
               "and a number the terminal picks. Recording a tap and playing "
               "it back later fails, because the numbers no longer match. "
               "Cloning a contactless card into a working one is not what this "
               "leak enables. Card-not-present fraud is.";
    default:
        return "Not on the chip. The card carries an account number, not an "
               "account. Some cards do carry a spending log, which is a "
               "different problem and one this app will show you.";
    }
}
