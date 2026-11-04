// ID checks for age-restricted items (tobacco, alcohol): ringing one in asks for the buyer's ID first.
// Scanning the back of the license checks the date of birth and expiration; a date of birth typed by
// hand is the exception (logged; a manager's PIN when the store asks). Once checked, the check
// remembers it: more restricted items on it go straight in. Settings: Site Setup > ID checks.
#include "app/pos_service.hh"
#include "core/id_card.hh"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

int PosService::requiredAge(const MenuItem &item) const
{
    const PosSettings &st = s_->settings;
    if (item.isModifier)
        return 0;
    int age = 0;
    if (st.idCheckAlcohol && item.taxClass == TaxClass::Alcohol)
        age = std::max(age, st.ageAlcohol);
    if (st.idCheckTobacco && std::ranges::find(st.tobaccoCategories, item.family) != st.tobaccoCategories.end())
        age = std::max(age, st.ageTobacco);
    return age;
}

bool PosService::askForAge(const MenuItem &item, int age, const PendingScan &scan)
{
    ageAsk_ = {{u"item"_s, qs(item.id)}, {u"name"_s, qs(item.name)}, {u"age"_s, age},
               {u"handNeedsManager"_s, s_->settings.idHandNeedsManager}};
    ageAskScan_ = scan;
    emit checkChanged();
    emit notice(tr("ID check: %1 is %2+. Scan the back of the ID.").arg(qs(item.name)).arg(age));
    return false;
}

QVariantMap PosService::idChecks() const
{
    const PosSettings &st = s_->settings;
    QStringList cats;
    for (const std::string &c : st.tobaccoCategories)
        cats << qs(c);
    return {{u"tobacco"_s, st.idCheckTobacco}, {u"alcohol"_s, st.idCheckAlcohol}, {u"ageTobacco"_s, st.ageTobacco},
            {u"ageAlcohol"_s, st.ageAlcohol}, {u"tobaccoCategories"_s, cats}, {u"handNeedsManager"_s, st.idHandNeedsManager}};
}

bool PosService::saveIdChecks(const QVariantMap &values)
{
    if (!require(perm::Manager, tr("ID check settings")))
        return false;
    PosSettings &st = s_->settings;
    if (values.contains(u"tobacco"_s))
        st.idCheckTobacco = values.value(u"tobacco"_s).toBool();
    if (values.contains(u"alcohol"_s))
        st.idCheckAlcohol = values.value(u"alcohol"_s).toBool();
    if (values.contains(u"ageTobacco"_s))
        st.ageTobacco = std::clamp(values.value(u"ageTobacco"_s).toInt(), 0, 99);
    if (values.contains(u"ageAlcohol"_s))
        st.ageAlcohol = std::clamp(values.value(u"ageAlcohol"_s).toInt(), 0, 99);
    if (values.contains(u"handNeedsManager"_s))
        st.idHandNeedsManager = values.value(u"handNeedsManager"_s).toBool();
    if (values.contains(u"tobaccoCategories"_s)) {
        st.tobaccoCategories.clear();
        for (const QString &c : values.value(u"tobaccoCategories"_s).toStringList())
            if (!c.trimmed().isEmpty())
                st.tobaccoCategories.push_back(ss(c.trimmed()));
    }
    settingsChanged();
    emit notice(tr("ID check settings saved"));
    return true;
}

// The ID passed: on the check, then the item waiting goes in.
bool PosService::agePassed(int age, const char *how, const std::string &idNumber, bool expired)
{
    const QString item = ageAsk_.value(u"item"_s).toString();
    const PendingScan scan = std::exchange(ageAskScan_, {});
    ageAsk_.clear();
    if (!current())   // the ID shown before anything was rung in
        startCheck(CheckType::Quick);
    if (Check *c = current()) {
        c->ageVerified = std::max(c->ageVerified, age);
        c->ageHow = how;
        c->ageBy = user() ? user()->name : std::string();
        c->ageAt = now();
        c->ageId4 = idNumber.size() > 4 ? idNumber.substr(idNumber.size() - 4) : idNumber;
        c->ageIdExpired = c->ageIdExpired || expired;
        noteEvent(*c, (std::string_view(how) == "hand" ? tr("ID checked by hand (date of birth typed): %1+").arg(age)
                                                       : tr("ID scanned: %1+").arg(age))
                          + (expired ? tr(", expired ID accepted") : QString()),
                  std::string_view(how) == "hand" ? "ageHand" : "ageScan");
        changed(*c);
    }
    emit checkChanged();
    if (item.isEmpty())
        return true;
    pendingScan_ = scan;
    return addItem(item);
}

bool PosService::checkAge(const IdDate &birth, const IdDate &expires, const char *how, const std::string &idNumber)
{
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    if (!birth.valid())
        return fail(tr("That date of birth isn't a real date."));
    // An expired ID still shows their photo and date of birth: accepted, noted on the check and receipt.
    const bool expired = expiredOn(expires, today.year(), today.month(), today.day());
    // The item waiting's age; an ID shown before anything is rung in: checked for both.
    const int need = ageAsk_.isEmpty() ? std::max({1, s_->settings.ageTobacco, s_->settings.ageAlcohol})
                                       : std::max(1, ageAsk_.value(u"age"_s).toInt());
    const int age = ageOn(birth, today.year(), today.month(), today.day());
    if (age < need) {
        const QString name = ageAsk_.value(u"name"_s).toString();
        if (Check *c = current())
            noteEvent(*c, tr("ID refused: under %1 (%2)").arg(need).arg(name), "ageRefused");
        ageAsk_.clear();
        ageAskScan_ = {};
        emit checkChanged();
        return fail(tr("Under %1: %2 can't be sold.").arg(need).arg(name));
    }
    if (!agePassed(need, how, idNumber, expired))
        return false;
    if (expired)
        emit notice(tr("Expired ID accepted: noted on the receipt."));
    return true;
}

bool PosService::verifyAgeScan(const QString &raw)
{
    if (!require(perm::Order, tr("ID checks")))
        return false;
    const auto card = parseIdCard(raw.toStdString());
    if (!card)
        return fail(tr("That isn't the barcode on the back of a license. Scan the back of the ID."));
    return checkAge(card->birth, card->expires, "scan", card->number);
}

bool PosService::verifyAgeHand(const QString &mmddyyyy)
{
    if (!require(perm::Order, tr("ID checks")))
        return false;
    QString d = mmddyyyy;
    d.remove(QRegularExpression(u"[^0-9]"_s));
    if (d.size() != 8)
        return fail(tr("Type the date of birth as MM DD YYYY."));
    if (s_->settings.idHandNeedsManager && !require(perm::Manager, tr("Typing a date of birth")))
        return false;
    const IdDate birth{d.mid(4, 4).toInt(), d.left(2).toInt(), d.mid(2, 2).toInt()};
    return checkAge(birth, {}, "hand", {});
}

void PosService::cancelAgeCheck()
{
    if (ageAsk_.isEmpty())
        return;
    ageAsk_.clear();
    ageAskScan_ = {};
    emit checkChanged();
}

} // namespace vt::app
